// Tier 4 - jdBasic GODOT.* native function suite.
//
// Registered on every JdbScriptInstance's VM right after boot eval. The
// natives are static C-callable functions (jdb_embed_register_native
// signature) that downcast their userdata pointer back to the
// JdbScriptInstance and call godot-cpp from there.
//
// Each instance keeps its own handle table mapping int64 -> Object*. The
// handles are plain jdBasic INT values; GODOT.SELF / GODOT.GET / etc
// return them, and any GODOT.* call that takes a "target object" expects
// such an int.

#pragma once

#ifdef GODOT

#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/variant/variant.hpp>
#include <godot_cpp/variant/callable.hpp>
#include <godot_cpp/variant/string_name.hpp>
#include <godot_cpp/core/object_id.hpp>

#include <unordered_map>
#include <vector>
#include <string>
#include <memory>

extern "C" {
struct JdbEmbed;
}

namespace godot {

class JdbScriptInstance;
class GodotBridge;

// Shared liveness token. A JdbSignalCallable keeps a shared_ptr to this so
// it can tell whether the bridge it dispatches into is still alive: a Godot
// signal connection can outlive the bridge (hot-reload tears the bridge
// down while the source Node, and therefore the connection, survive). When
// the bridge dies it flips `alive` to false and any late-firing callable
// becomes an inert no-op instead of a dangling-pointer call.
struct BridgeAlive {
    GodotBridge* bridge = nullptr;
    bool         alive  = true;
};

// One per JdbScriptInstance - lives as long as the script does.
class GodotBridge {
public:
    GodotBridge(JdbEmbed* vm, JdbScriptInstance* owner);
    ~GodotBridge();

    // Register the full GODOT.* suite on the embed VM.
    void register_all();

    // Handle -> Object* lookup.
    Object* lookup(int64_t handle) const;
    int64_t store(Object* obj);

    JdbEmbed*          vm()    const { return m_vm; }
    JdbScriptInstance* owner() const { return m_owner; }

    // ── Signals (GODOT.CONNECT / GODOT.DISCONNECT) ────────────────────
    // Wire a Godot signal on the object behind `obj_handle` to a jdBasic
    // SUB. Returns 0 on success, -1 if the handle resolves to no object.
    int  connect_signal(int64_t obj_handle, const String& sig,
                        const String& sub, uint32_t flags);
    bool disconnect_signal(int64_t obj_handle, const String& sig,
                            const String& sub);

    // Called from a JdbSignalCallable when a connected signal fires. Builds
    // `sub(arg, ...)` and re-enters the VM (or defers it if we're already
    // mid-callback - see m_callback_depth).
    void dispatch_signal(const String& sub, const Variant** args, int argc);

    // GODOT.TIMER - spawn a Timer child on the owning Node, wire its
    // timeout to `sub`, and start it. repeat=false makes a one-shot that
    // frees itself after firing. Returns a bridge handle to the Timer (0
    // if there's no owner Node to parent it to).
    int64_t make_timer(double secs, const String& sub, bool repeat);

    // Re-entrancy fence around every VM eval that runs an engine callback
    // (_process / _input / a signal dispatch). While depth > 0 any further
    // signal dispatch is queued and drained when depth returns to 0, so we
    // never nest jdb_embed_eval on a single VM.
    void enter_callback();
    void leave_callback();

private:
    // Serialize one signal argument into a jdBasic source literal. Objects
    // are stored in the handle table and emitted as their int handle so the
    // SUB receives something GODOT.GET/SET/CALL can resolve.
    std::string arg_literal_(const Variant& v);
    void        run_call_(const std::string& code);
    // Drop connection records whose source Object has been freed (e.g. a
    // one-shot timer that queue_freed itself) so the list stays bounded.
    void        prune_dead_();

    struct ConnRec {
        ObjectID   src;
        StringName sig;
        String     sub;
        Callable   cb;
    };

    JdbEmbed*                              m_vm    = nullptr;
    JdbScriptInstance*                     m_owner = nullptr;
    // Handle -> ObjectID (not raw Object*) so lookup() can validate against
    // ObjectDB and hand back a freed object as null instead of dangling.
    std::unordered_map<int64_t, uint64_t>  m_table;
    int64_t                                m_next_handle = 1;

    std::shared_ptr<BridgeAlive>           m_alive;
    std::vector<ConnRec>                   m_connections;
    int                                    m_callback_depth = 0;
    std::vector<std::string>               m_deferred;
};

// Conversion helpers used by the natives. Variant -> jdBasic JdbValue,
// and the reverse direction. Vectors / Colors / similar go through
// 3-or-4-element jdBasic arrays so we don't have to invent new value tags.
// Variant::OBJECT lands as a jdBasic INT carrying the bridge handle that
// the GODOT.* natives can later resolve back to an Object*.
int64_t variant_to_jdb_value(GodotBridge* bridge, const Variant& v);
Variant jdb_value_to_variant(GodotBridge* bridge, int64_t h);

}  // namespace godot

#endif  // GODOT

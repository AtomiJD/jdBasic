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

#include <unordered_map>

extern "C" {
struct JdbEmbed;
}

namespace godot {

class JdbScriptInstance;

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

private:
    JdbEmbed*                              m_vm    = nullptr;
    JdbScriptInstance*                     m_owner = nullptr;
    std::unordered_map<int64_t, Object*>   m_table;
    int64_t                                m_next_handle = 1;
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

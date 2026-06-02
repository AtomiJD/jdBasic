// JdbScriptInstance - per-Node attached-script runtime.
//
// Each instance owns its own jdBasic VM. When Godot attaches a .jdb to a
// Node and the Node enters the tree, the engine calls into the
// `GDExtensionScriptInstanceInfo3` callback table - we route those calls
// (most importantly `call`) into jdBasic FUNC invocations.
//
// This class is NOT registered with ClassDB. The instance handle that
// Godot tracks comes from `gdextension_interface::script_instance_create3`,
// and the function pointers in `JdbScriptInstance::s_info` are static C
// bouncers that downcast the opaque data pointer back to a
// JdbScriptInstance*.

#pragma once

#ifdef GODOT

#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/string_name.hpp>
#include <godot_cpp/variant/variant.hpp>

#include <gdextension_interface.h>

#include <godot_cpp/variant/dictionary.hpp>

#include <unordered_set>
#include <string>
#include <vector>

extern "C" {
struct JdbEmbed;
}

namespace godot {

class JdbScriptResource;
class Object;
class ScriptLanguage;

class JdbScriptInstance {
public:
    JdbScriptInstance(Ref<JdbScriptResource> p_script, Object* p_owner);
    ~JdbScriptInstance();

    // Returns the GDExtension instance pointer Godot tracks. Filled
    // in once via gdextension_interface::script_instance_create3.
    GDExtensionScriptInstancePtr godot_handle() const { return m_godot_handle; }
    void set_godot_handle(GDExtensionScriptInstancePtr h) { m_godot_handle = h; }

    // ── Backing for the C callback bouncers ─────────────────────
    Object*         get_owner()    const { return m_owner;  }
    Ref<JdbScriptResource> get_script() const { return m_script; }

    bool    has_method(const StringName& name) const;
    Variant call_method(const StringName& name, const Variant** args, int64_t argc);

    // Inspector property access.
    struct InspectorVar {
        StringName       name;
        Variant::Type    type;
        StringName       class_name;  // empty for primitives
        int              hint = 0;    // PROPERTY_HINT_*
        String           hint_string; // RANGE("min,max"), ENUM("a,b,c"), FILE("*.png")
    };
    const std::vector<InspectorVar>& inspector_vars() const { return m_inspector_vars; }
    bool    has_property(const StringName& name) const;
    Variant get_property(const StringName& name);
    bool    set_property(const StringName& name, const Variant& value);

    // Hot reload (Script._reload with keep_state=true). Merges new FUNC/SUB
    // bodies via jdb_embed_recompile_source; globals + state preserved.
    bool    hot_recompile(const String& processed_src);
    // Hard reload (keep_state=false). Drops the VM, creates a fresh one,
    // re-evals from scratch. Any non-INSPECTOR state is lost.
    bool    hard_reload(const String& processed_src);

    // The shared info table that backs every JdbScriptInstance.
    static const GDExtensionScriptInstanceInfo3 s_info;

    // Diagnostic - how many instances are alive right now.
    static int alive_count();

    // E3 - convert a jdBasic value handle into the closest matching Godot
    // Variant type. Numeric arrays become PackedFloat64Array; mixed arrays
    // become generic Array; OBJECT becomes Dictionary; etc. The handle is
    // NOT released - caller is responsible.
    static Variant value_to_variant(JdbEmbed* vm, int64_t handle);

private:
    Ref<JdbScriptResource>             m_script;
    Object*                            m_owner = nullptr;
    JdbEmbed*                          m_vm = nullptr;
    GDExtensionScriptInstancePtr       m_godot_handle = nullptr;
    std::unordered_set<std::string>    m_method_set;     // lower-cased
    std::vector<InspectorVar>          m_inspector_vars; // mirror of script metadata

    // Editor-mode property cache. When the instance has no VM (we're in
    // the Inspector, not playing the game), get_property reads from here
    // and set_property writes here so the .tscn override round-trip works.
    Dictionary                         m_editor_values;

    // Tier 4 - GODOT.* native suite. Lives as long as the instance does.
    class GodotBridge*                 m_bridge = nullptr;

    static std::string variant_to_jdb_arg_(const Variant& v);
    void scan_methods_(const String& source);
    // Godot only auto-enables _process from a script's method list, not the
    // input callbacks. Mirror the scanned method set onto the owning Node's
    // processing flags so _input / _unhandled_input / _physics_process fire.
    void enable_node_processing_();
};

}  // namespace godot

#endif  // GODOT

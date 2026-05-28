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

#include <unordered_set>
#include <string>

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

    // The shared info table that backs every JdbScriptInstance.
    static const GDExtensionScriptInstanceInfo3 s_info;

private:
    Ref<JdbScriptResource>             m_script;
    Object*                            m_owner = nullptr;
    JdbEmbed*                          m_vm = nullptr;
    GDExtensionScriptInstancePtr       m_godot_handle = nullptr;
    std::unordered_set<std::string>    m_method_set;   // lower-cased

    static std::string variant_to_jdb_arg_(const Variant& v);
    void scan_methods_(const String& source);
};

}  // namespace godot

#endif  // GODOT

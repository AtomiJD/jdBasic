// JdbScriptResource - a .jdb file as a Godot Resource.
//
// T3.0 surface: source-code round-trip + base-type stub. Per-Node
// instance creation and engine-callback dispatch arrive in T3.2 via the
// GDExtensionScriptInstanceInfo3 bridge.
//
// Naming note: this class is named `JdbScriptResource` to avoid the
// collision with Tier 2's `JDBScript` Node class. The user-facing
// language label in Godot stays "jdBasic" (see JdbScriptLanguage::_get_name).

#pragma once

#ifdef GODOT

#include <godot_cpp/classes/script_extension.hpp>
#include <godot_cpp/classes/script_language.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/string_name.hpp>
#include <godot_cpp/variant/typed_array.hpp>

namespace godot {

class JdbScriptResource : public ScriptExtension {
    GDCLASS(JdbScriptResource, ScriptExtension)

protected:
    static void _bind_methods();

public:
    JdbScriptResource();
    ~JdbScriptResource();

    // Source code persistence (Godot's editor saves via this).
    bool       _has_source_code()                       const override;
    String     _get_source_code()                       const override;
    void       _set_source_code(const String& p_code)         override;

    // Identity / capability stubs - just enough for T3.0 to register
    // cleanly. T3.1 hooks _validate and the introspection methods.
    bool       _can_instantiate()                       const override;
    bool       _is_valid()                              const override;
    bool       _is_tool()                               const override;
    StringName _get_instance_base_type()                const override;
    bool       _has_method(const StringName& method)    const override;
    bool       _has_property_default_value(const StringName& p_property) const override;
    Error      _reload(bool p_keep_state)                     override;
    ScriptLanguage* _get_language()                     const override;

    // Required virtual stub; real doc extraction is the T3.6 stretch.
    TypedArray<Dictionary> _get_documentation()         const override;

    // T3.2 instance bridge - creates a JdbScriptInstance and hands its
    // GDExtension handle back to Godot.
    void*      _instance_create(Object* p_for_object)   const override;

    // Path-B preprocessing outputs - filled by _set_source_code.
    String                 get_processed_source() const { return m_source_processed; }
    StringName             get_extends_type()     const { return m_extends_type;     }
    const TypedArray<Dictionary>& get_inspector_vars() const { return m_inspector_vars; }

private:
    // User-typed source as Godot edits / saves it.
    String m_source;

    // Path-B outputs (rebuilt every _set_source_code call).
    String                 m_source_processed;  // what jdBasic eats
    StringName             m_extends_type;      // from EXTENDS Foo line
    TypedArray<Dictionary> m_inspector_vars;    // [{name, default, type}]

    void preprocess_();
};

}  // namespace godot

#endif  // GODOT

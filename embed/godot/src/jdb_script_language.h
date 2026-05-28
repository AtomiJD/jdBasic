// JdbScriptLanguage - the Tier-3 ScriptLanguageExtension that makes
// jdBasic a peer scripting language alongside GDScript / C# in Godot.
//
// T3.0 surface (this file): the minimum getters Godot needs to enumerate
// the language and offer it in the Attach-Script dropdown. _create_script
// returns a JdbScript instance. Per-script behaviour (source storage,
// instance creation, callback dispatch) lives in JdbScript and the
// GDExtensionScriptInstanceInfo3 bridge - not in this language singleton.
//
// This is the GODOT-only path - Tier 1 (`JDBasicVM`) and Tier 2 (`JDBScript`)
// remain unchanged.

#pragma once

#ifdef GODOT

#include <godot_cpp/classes/script_language_extension.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>

namespace godot {

class JdbScriptLanguage : public ScriptLanguageExtension {
    GDCLASS(JdbScriptLanguage, ScriptLanguageExtension)

protected:
    static void _bind_methods();

public:
    JdbScriptLanguage();
    ~JdbScriptLanguage();

    static JdbScriptLanguage* get_singleton();

    // Required identity getters.
    String              _get_name()                  const override;
    String              _get_type()                  const override;
    String              _get_extension()             const override;
    PackedStringArray   _get_recognized_extensions() const override;

    // Lifecycle. Most of the editor / debugger / profiler surface stays
    // at base-class defaults in T3.0; we override the bare minimum needed
    // for the language to register and survive Godot's startup.
    void                _init()                            override;
    void                _finish()                          override;

    // Editor-side syntax hints (T3.5 polish; defaults are fine for T3.0).
    PackedStringArray   _get_reserved_words()        const override;
    PackedStringArray   _get_comment_delimiters()    const override;
    PackedStringArray   _get_string_delimiters()     const override;

    bool                _has_named_classes()         const override;
    bool                _supports_builtin_mode()     const override;
    bool                _can_inherit_from_file()     const override;

    // Hand back a fresh JdbScript resource. Godot calls this when the
    // user clicks "Create" in the Attach-Script dialog.
    Object*             _create_script()             const override;

    // Boilerplate for a freshly-created .jdb file. Without overriding
    // this, Godot dereferences a null Ref<Script> and crashes.
    Ref<Script>         _make_template(const String& p_template,
                                       const String& p_class_name,
                                       const String& p_base_class_name) const override;
    bool                _is_using_templates()              override;

    // Validation - T3.0 always returns "ok"; T3.1 will plug into
    // jdBasic's on_check (lex+parse only) for real diagnostics.
    Dictionary          _validate(const String& p_script,
                                  const String& p_path,
                                  bool p_validate_functions,
                                  bool p_validate_errors,
                                  bool p_validate_warnings,
                                  bool p_validate_safe_lines) const override;

private:
    static JdbScriptLanguage* s_singleton;
};

}  // namespace godot

#endif  // GODOT

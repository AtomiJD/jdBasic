// JdbScriptLanguage - T3.0 implementation. See header for scope.

#ifdef GODOT

#include "jdb_script_language.h"
#include "jdb_script_resource.h"

#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/classes/script.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>

using namespace godot;

JdbScriptLanguage* JdbScriptLanguage::s_singleton = nullptr;

void JdbScriptLanguage::_bind_methods() {
    // No public methods exported yet - everything bridges via the
    // overridden virtuals.
}

JdbScriptLanguage::JdbScriptLanguage() {
    if (!s_singleton) s_singleton = this;
}

JdbScriptLanguage::~JdbScriptLanguage() {
    if (s_singleton == this) s_singleton = nullptr;
}

JdbScriptLanguage* JdbScriptLanguage::get_singleton() {
    return s_singleton;
}

String JdbScriptLanguage::_get_name() const {
    return String("jdBasic");
}

String JdbScriptLanguage::_get_type() const {
    // The script-resource class name. Must match the C++ class registered
    // via ClassDB::register_class<JdbScriptResource>() (see register_types.cpp).
    return String("JdbScriptResource");
}

String JdbScriptLanguage::_get_extension() const {
    return String("jdb");
}

PackedStringArray JdbScriptLanguage::_get_recognized_extensions() const {
    PackedStringArray exts;
    exts.push_back(String("jdb"));
    return exts;
}

void JdbScriptLanguage::_init() {
    // T3.0: no-op. T3.1+ will set up any shared parser state here.
}

void JdbScriptLanguage::_finish() {
    // T3.0: no-op.
}

PackedStringArray JdbScriptLanguage::_get_reserved_words() const {
    // Keep the list short for T3.0; the syntax-highlighting tier (T3.5)
    // will fill in the full token set from doc/languages.md.
    static const char* kw[] = {
        "DIM", "EXPORT", "INSPECTOR", "EXTENDS", "FUNC", "SUB", "ENDFUNC",
        "ENDSUB", "RETURN", "IF", "THEN", "ELSE", "ENDIF", "FOR", "TO", "STEP",
        "NEXT", "WHILE", "WEND", "DO", "LOOP", "UNTIL", "IMPORT", "MODULE",
        "AND", "OR", "NOT", "TRUE", "FALSE", "NULL", "AS", "INTEGER",
        "DOUBLE", "STRING", "ARRAY", "MAP", "PRINT", "INPUT", "OPTION",
        "ON", "OFF", "ANDALSO", "ORELSE",
    };
    PackedStringArray r;
    for (const char* w : kw) r.push_back(String(w));
    return r;
}

PackedStringArray JdbScriptLanguage::_get_comment_delimiters() const {
    PackedStringArray r;
    r.push_back(String("'"));  // jdBasic line comment
    return r;
}

PackedStringArray JdbScriptLanguage::_get_string_delimiters() const {
    PackedStringArray r;
    r.push_back(String("\" \""));  // double-quoted string
    return r;
}

bool JdbScriptLanguage::_has_named_classes() const {
    return false;  // No class declarations in jdBasic-as-Godot-script (yet).
}

bool JdbScriptLanguage::_supports_builtin_mode() const {
    return false;  // .jdb is always a separate file, not inline-in-tscn.
}

bool JdbScriptLanguage::_can_inherit_from_file() const {
    return false;  // No script inheritance yet.
}

Object* JdbScriptLanguage::_create_script() const {
    // Godot owns the returned pointer; ClassDB will refcount-manage as
    // appropriate based on the class registration.
    return memnew(JdbScriptResource);
}

Ref<Script> JdbScriptLanguage::_make_template(const String& /*p_template*/,
                                              const String& p_class_name,
                                              const String& p_base_class_name) const {
    String base = p_base_class_name.is_empty() ? String("Node") : p_base_class_name;
    String cls  = p_class_name.is_empty()      ? String("Untitled") : p_class_name;

    String src;
    src += String("EXTENDS ") + base + String("\n");
    src += String("\n");
    src += String("' ") + cls + String(" -- jdBasic script\n");
    src += String("\n");
    src += String("INSPECTOR DIM speed = 1.0\n");
    src += String("\n");
    src += String("SUB _ready()\n");
    src += String("\tPRINT \"") + cls + String(" ready\"\n");
    src += String("ENDSUB\n");
    src += String("\n");
    src += String("SUB _process(delta)\n");
    src += String("ENDSUB\n");

    Ref<JdbScriptResource> script;
    script.instantiate();
    script->_set_source_code(src);
    return script;
}

bool JdbScriptLanguage::_is_using_templates() {
    return true;
}

void JdbScriptLanguage::_frame() {
    // No-op. Required-virtual stub; Godot calls every frame for every
    // registered language. T3.7 hooks profiling here if we ever want it.
}

void JdbScriptLanguage::_reload_scripts(const Array& p_scripts, bool p_soft_reload) {
    // Forward to each Script's own _reload. We don't batch-optimise yet;
    // hot-reload of a single script per tick is the common case.
    for (int i = 0; i < p_scripts.size(); ++i) {
        Ref<JdbScriptResource> s = p_scripts[i];
        if (s.is_valid()) {
            s->_reload(p_soft_reload);
        }
    }
}

Dictionary JdbScriptLanguage::_complete_code(const String& /*p_code*/,
                                              const String& /*p_path*/,
                                              Object* /*p_owner*/) const {
    // T3.0 stub. Real autocomplete is the T3.6 stretch goal.
    Dictionary d;
    d[String("result")]  = Error::OK;
    d[String("force")]   = false;
    d[String("call_hint")] = String();
    d[String("options")] = Array();
    return d;
}

Dictionary JdbScriptLanguage::_lookup_code(const String& /*p_code*/,
                                            const String& /*p_symbol*/,
                                            const String& /*p_path*/,
                                            Object* /*p_owner*/) const {
    // T3 stub. T3.6 wires this into the jdBasic symbol table so Ctrl-click
    // jumps to definitions. For now we report "unavailable" cleanly so
    // Godot's editor doesn't spam the console.
    Dictionary d;
    d[String("result")] = Error::ERR_UNAVAILABLE;
    return d;
}

Dictionary JdbScriptLanguage::_validate(const String& /*p_script*/,
                                       const String& /*p_path*/,
                                       bool /*p_validate_functions*/,
                                       bool /*p_validate_errors*/,
                                       bool /*p_validate_warnings*/,
                                       bool /*p_validate_safe_lines*/) const {
    // T3.0 stub: always-ok. T3.1 wires this into the jdBasic check path.
    Dictionary d;
    d[String("valid")]                  = true;
    d[String("errors")]                 = Array();
    d[String("warnings")]               = Array();
    d[String("functions")]              = Array();
    d[String("safe_lines")]             = Array();
    return d;
}

#endif  // GODOT

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

// Returns UPPER, lower, and Title forms of a keyword so Godot's editor
// (which matches reserved words case-sensitively) highlights them no
// matter how the user types them. jdBasic itself is case-insensitive.
static void push_case_variants(PackedStringArray& out, const char* kw) {
    String upper(kw);
    String lower = upper.to_lower();
    String title = upper.substr(0, 1) + upper.substr(1, upper.length() - 1).to_lower();
    out.push_back(upper);
    if (lower != upper) out.push_back(lower);
    if (title != upper && title != lower) out.push_back(title);
}

PackedStringArray JdbScriptLanguage::_get_reserved_words() const {
    // Canonical jdBasic keyword set, mirrored from syntaxes/jdbasic.tmLanguage.json
    // so Godot's editor colours the same words as VS Code / the existing
    // tooling. Tier-3-specific additions: EXTENDS, INSPECTOR.
    static const char* kw[] = {
        // Control flow (also reported via _is_control_flow_keyword for
        // the secondary highlight colour).
        "IF","THEN","ELSE","ELSEIF","ENDIF",
        "FOR","TO","STEP","NEXT","EACH","IN",
        "DO","LOOP","WHILE","WEND","UNTIL",
        "SWITCH","CASE","DEFAULT","ENDSWITCH","EXITSWITCH",
        "SUB","ENDSUB","FUNC","ENDFUNC",
        "RETURN","GOTO","CALL",
        "EXIT","EXITFUNC","EXITDO","EXITFOR","EXITSUB",
        "CONTINUEFOR","CONTINUEDO","CONTINUELOOP",
        "TRY","CATCH","FINALLY","ENDTRY","THROW",
        "AWAIT","ASYNC",
        // Declarations / structure
        "CONST","DIM","STATIC","AS","LET",
        "TYPE","ENDTYPE","ENUM","ENDENUM","THIS","REACT",
        "IMPORT","EXPORT","MODULE","DECLARE","OPTION",
        "STOP","RESUME","CHAN","END",
        // Tier-3 additions
        "EXTENDS","INSPECTOR",
        // Built-in types
        "INTEGER","DOUBLE","STRING","MAP","ARRAY","DYNAMIC","TENSOR",
        "JSON","DATE","BOOLEAN","BOOL","BYTE","CHAR",
        "INT16","INT32","INT64","FLOAT16","FLOAT32","FLOAT64","OBJECT",
        // Common commands / built-ins
        "PRINT","INPUT","CLS","COLOR","LOCATE","CURSOR","SLEEP","REM",
        "LIST","RUN","NEW","COMPILE","EXECUTE","EVAL","LAMBDA",
        // Constants
        "TRUE","FALSE","PI","E","VBNEWLINE","VBCRLF","VBTAB",
        "NONE","NULL","INF","NAN",
        // Operators / boolean (highlighted as keywords)
        "AND","OR","NOT","XOR","MOD","ANDALSO","ORELSE",
        "BAND","BOR","BXOR","BNOT","SHL","SHR",
    };
    PackedStringArray r;
    for (const char* w : kw) push_case_variants(r, w);
    return r;
}

PackedStringArray JdbScriptLanguage::_get_comment_delimiters() const {
    PackedStringArray r;
    r.push_back(String("'"));    // apostrophe -> end of line
    r.push_back(String("REM"));  // REM keyword -> end of line
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

bool JdbScriptLanguage::_supports_documentation() const {
    return false;  // No doc extraction yet.
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

void JdbScriptLanguage::_thread_enter() {}
void JdbScriptLanguage::_thread_exit()  {}

bool JdbScriptLanguage::_handles_global_class_type(const String& /*p_type*/) const {
    // No global class registration yet (we don't implement
    // `class_name Foo`-style declarations in jdBasic-as-Godot-script).
    return false;
}

bool JdbScriptLanguage::_overrides_external_editor() {
    return false;
}

bool JdbScriptLanguage::_is_control_flow_keyword(const String& p_keyword) const {
    static const char* kw[] = {
        "IF","THEN","ELSE","ELSEIF","ENDIF",
        "FOR","TO","STEP","NEXT","EACH","IN",
        "DO","LOOP","WHILE","WEND","UNTIL",
        "SWITCH","CASE","DEFAULT","ENDSWITCH","EXITSWITCH",
        "RETURN","GOTO","CALL",
        "EXIT","EXITFUNC","EXITDO","EXITFOR","EXITSUB",
        "CONTINUEFOR","CONTINUEDO","CONTINUELOOP",
        "TRY","CATCH","FINALLY","ENDTRY","THROW",
        "AWAIT","ASYNC",
    };
    String upper = p_keyword.to_upper();
    for (const char* k : kw) {
        if (upper == String(k)) return true;
    }
    return false;
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

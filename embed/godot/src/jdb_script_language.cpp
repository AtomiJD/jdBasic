// JdbScriptLanguage - T3.0 implementation. See header for scope.

#ifdef GODOT

#include "jdb_script_language.h"
#include "jdb_script_resource.h"
#include "jdb_embed_api.h"

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
    // Apostrophe is the only delimiter Godot's editor highlighter
    // accepts; REM is a keyword (no leading symbol) so Godot rejects it
    // with "delimiter must start with a symbol". REM is still in the
    // reserved-words list, so it colours as a keyword rather than starting
    // a comment region - close enough for the editor experience.
    r.push_back(String("'"));
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

TypedArray<Dictionary> JdbScriptLanguage::_get_built_in_templates(const StringName& p_object) const {
    // Each template entry expects:
    //   inherit:     base class (StringName / String) - filters which
    //                templates show up for which Node type
    //   name:        short label shown in the template dropdown
    //   description: tooltip
    //   content:     the .jdb source. %CLASS% / %BASE% get string-replaced
    //                by Godot when the user clicks Create.
    //   id, origin:  0 / 0 for built-in
    TypedArray<Dictionary> out;
    String base = String(p_object).is_empty() ? String("Node") : String(p_object);

    static const String K_INHERIT     = String("inherit");
    static const String K_NAME        = String("name");
    static const String K_DESCRIPTION = String("description");
    static const String K_CONTENT     = String("content");
    static const String K_ID          = String("id");
    static const String K_ORIGIN      = String("origin");

    // ── Default - empty body ─────────────────────────────────────
    {
        Dictionary d;
        d[K_INHERIT]     = base;
        d[K_NAME]        = String("Empty");
        d[K_DESCRIPTION] = String("Minimal jdBasic script with the engine hooks stubbed out.");
        String content;
        content += String("EXTENDS ") + base + String("\n");
        content += String("\n");
        content += String("' %CLASS% - jdBasic script\n");
        content += String("\n");
        content += String("SUB _ready()\n");
        content += String("ENDSUB\n");
        content += String("\n");
        content += String("SUB _process(delta)\n");
        content += String("ENDSUB\n");
        d[K_CONTENT]    = content;
        d[K_ID]         = 0;
        d[K_ORIGIN]     = 0;
        out.append(d);
    }

    // ── INSPECTOR-DIM showcase ──────────────────────────────────
    {
        Dictionary d;
        d[K_INHERIT]     = base;
        d[K_NAME]        = String("With Inspector vars");
        d[K_DESCRIPTION] = String("Exposes speed + colour as INSPECTOR DIM with RANGE/COLOR hints.");
        String content;
        content += String("EXTENDS ") + base + String("\n");
        content += String("\n");
        content += String("INSPECTOR DIM speed = 1.0 AS RANGE(0.0, 10.0, 0.1)\n");
        content += String("INSPECTOR DIM tint  = 0.5 AS RANGE(0.0, 1.0, 0.01)\n");
        content += String("\n");
        content += String("DIM self_h = 0\n");
        content += String("\n");
        content += String("SUB _ready()\n");
        content += String("\tself_h = GODOT.SELF()\n");
        content += String("\tGODOT.PRINT(\"%CLASS% ready - speed =\", speed)\n");
        content += String("ENDSUB\n");
        content += String("\n");
        content += String("SUB _process(delta)\n");
        content += String("ENDSUB\n");
        d[K_CONTENT]    = content;
        d[K_ID]         = 1;
        d[K_ORIGIN]     = 0;
        out.append(d);
    }

    // ── @tool template ──────────────────────────────────────────
    {
        Dictionary d;
        d[K_INHERIT]     = base;
        d[K_NAME]        = String("@tool");
        d[K_DESCRIPTION] = String("Runs in the editor too - good for procedural meshes / editor helpers.");
        String content;
        content += String("' @tool\n");
        content += String("EXTENDS ") + base + String("\n");
        content += String("\n");
        content += String("DIM self_h = 0\n");
        content += String("\n");
        content += String("SUB _ready()\n");
        content += String("\tself_h = GODOT.SELF()\n");
        content += String("\tGODOT.PRINT(\"%CLASS% (tool) ready\")\n");
        content += String("ENDSUB\n");
        content += String("\n");
        content += String("SUB _process(delta)\n");
        content += String("ENDSUB\n");
        d[K_CONTENT]    = content;
        d[K_ID]         = 2;
        d[K_ORIGIN]     = 0;
        out.append(d);
    }

    // ── Spinner (Node3D-flavoured pre-built demo) ──────────────
    if (base == String("Node3D")) {
        Dictionary d;
        d[K_INHERIT]     = base;
        d[K_NAME]        = String("Spinner (Node3D)");
        d[K_DESCRIPTION] = String("Continuously rotates the Node3D on Y using GODOT.SET.");
        String content;
        content += String("EXTENDS Node3D\n");
        content += String("\n");
        content += String("INSPECTOR DIM rot_speed = 1.0 AS RANGE(0.0, 10.0, 0.1)\n");
        content += String("\n");
        content += String("DIM self_h = 0\n");
        content += String("DIM angle  = 0.0\n");
        content += String("\n");
        content += String("SUB _ready()\n");
        content += String("\tself_h = GODOT.SELF()\n");
        content += String("ENDSUB\n");
        content += String("\n");
        content += String("SUB _process(delta)\n");
        content += String("\tangle = angle + rot_speed * delta\n");
        content += String("\tGODOT.SET(self_h, \"rotation:y\", angle)\n");
        content += String("ENDSUB\n");
        d[K_CONTENT]    = content;
        d[K_ID]         = 3;
        d[K_ORIGIN]     = 0;
        out.append(d);
    }

    return out;
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

void JdbScriptLanguage::_reload_all_scripts() {
    // No-op for now. The Project menu's "Reload Scripts" trigger goes
    // through here; individual _reload calls handle the per-script side.
}

void JdbScriptLanguage::_reload_tool_script(const Ref<Script>& p_script,
                                              bool p_soft_reload) {
    // @tool scripts go through this path on file-save instead of the
    // generic Script::_reload. Forward to our Resource's _reload so the
    // live-instance fan-out + recompile actually runs.
    Ref<JdbScriptResource> s = p_script;
    if (s.is_valid()) {
        s->_reload(p_soft_reload);
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
    // jumps to definitions. Godot also expects a "type" key in the result
    // dict otherwise it pushes "Condition !ret.has('type') is true" errors.
    Dictionary d;
    d[String("result")]   = Error::ERR_UNAVAILABLE;
    d[String("type")]     = 0;     // LOOKUP_RESULT_SCRIPT_LOCATION sentinel
    d[String("location")] = -1;
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

bool JdbScriptLanguage::_can_make_function() const {
    return false;  // Editor's "create empty function" template flow off for now.
}

int32_t JdbScriptLanguage::_find_function(const String& /*p_function*/,
                                            const String& /*p_code*/) const {
    return -1;  // T5.6 hooks the symbol-table lookup.
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

Dictionary JdbScriptLanguage::_validate(const String& p_script,
                                       const String& p_path,
                                       bool /*p_validate_functions*/,
                                       bool /*p_validate_errors*/,
                                       bool /*p_validate_warnings*/,
                                       bool /*p_validate_safe_lines*/) const {
    (void)p_path;
    Dictionary d;
    Array errors;
    Array warnings;
    Array functions;
    Array safe_lines;
    bool valid = true;

    // T5.1: route through the standalone embed check (lex + parse only,
    // no VM state). jdBasic reports "Parse error at line N: ..." or
    // "Lex error at line N: ..."; we scrape the line number out so
    // Godot's editor can point at the right row.
    //
    // CRITICAL: the editor hands us the raw source with EXTENDS /
    // INSPECTOR DIM lines that jdBasic-core doesn't recognise. Push the
    // text through Path-B preprocessing first (using a throwaway
    // JdbScriptResource as the engine) so the check sees the same code
    // the runtime would. Line numbers stay aligned - preprocess only
    // rewrites in place, never adds or drops lines.
    Ref<JdbScriptResource> staging;
    staging.instantiate();
    staging->_set_source_code(p_script);
    String processed = staging->get_processed_source();
    if (processed.is_empty()) processed = p_script;

    CharString src_utf8 = processed.utf8();
    char* err = jdb_embed_check_standalone(src_utf8.get_data());
    if (err) {
        valid = false;
        String msg = String::utf8(err);
        jdb_embed_free(err);

        int line = 0;
        int at_line = msg.find("line ");
        if (at_line >= 0) {
            String tail = msg.substr(at_line + 5, msg.length() - (at_line + 5));
            // Walk forward while digit; stop at first non-digit.
            int end = 0;
            while (end < tail.length()) {
                char32_t c = tail[end];
                if (c < '0' || c > '9') break;
                ++end;
            }
            if (end > 0) line = tail.substr(0, end).to_int();
        }

        Dictionary e;
        e[String("line")]    = line;
        e[String("column")]  = 1;
        e[String("message")] = msg;
        e[String("path")]    = String();
        errors.append(e);
    }

    d[String("valid")]     = valid;
    d[String("errors")]    = errors;
    d[String("warnings")]  = warnings;
    d[String("functions")] = functions;
    d[String("safe_lines")] = safe_lines;
    return d;
}

#endif  // GODOT

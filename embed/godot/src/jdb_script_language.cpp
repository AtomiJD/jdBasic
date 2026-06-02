// JdbScriptLanguage - T3.0 implementation. See header for scope.

#ifdef GODOT

#include "jdb_script_language.h"
#include "jdb_script_resource.h"
#include "jdb_script_instance.h"
#include "jdb_embed_api.h"

#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/classes/script.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/color.hpp>
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

Dictionary JdbScriptLanguage::_complete_code(const String& p_code,
                                              const String& /*p_path*/,
                                              Object* /*p_owner*/) const {
    Dictionary d;
    Array options;

    static const String K_KIND        = String("kind");
    static const String K_DISPLAY     = String("display");
    static const String K_INSERT_TEXT = String("insert_text");
    static const String K_LOCATION    = String("location");
    static const String K_FONT_COLOR  = String("font_color");
    static const String K_ICON        = String("icon");
    static const String K_MATCHES     = String("matches");
    static const String K_DEFAULT_VAL = String("default_value");

    // CodeCompletionKind enum values from Godot's TextEdit / CodeEdit:
    //   0=CLASS, 1=FUNCTION, 2=SIGNAL, 3=VARIABLE, 4=MEMBER, 5=ENUM,
    //   6=CONSTANT, 7=NODE_PATH, 8=FILE_PATH, 9=PLAIN_TEXT
    //
    // Godot's wrapper skips any option missing the font_color / icon /
    // matches keys (logs "Condition ... is true. Continuing." for each).
    // We give every option the same default white tint and an empty
    // icon so the editor falls back to its built-in CodeCompletionKind
    // glyphs; matches stays empty since we don't pre-fuzz the list -
    // Godot's CodeEdit does substring filtering itself.
    auto push_opt = [&](int kind, const String& display,
                        const String& insert, int location) {
        Dictionary opt;
        opt[K_KIND]        = kind;
        opt[K_DISPLAY]     = display;
        opt[K_INSERT_TEXT] = insert;
        opt[K_LOCATION]    = location;
        opt[K_FONT_COLOR]  = Color(1, 1, 1, 1);
        opt[K_ICON]        = Variant();
        opt[K_DEFAULT_VAL] = Variant();   // required - or ERR_CONTINUE drops the option
        // 'matches' is optional and uses PackedInt32Array; we let Godot
        // compute matches itself by leaving it absent.
        options.append(opt);
    };

    // Figure out what's right before the cursor so dotted-name
    // completions don't duplicate the qualifier. Godot replaces only the
    // partial-word at the cursor with insert_text, leaving anything before
    // the last word-boundary alone - typing "GODOT." and picking "GODOT.SET"
    // would otherwise produce "GODOT.GODOT.SET". Detect a "<qualifier>."
    // prefix; if it's a known namespace, send only the suffix as insert_text
    // while keeping the full name in display so the user sees what they're
    // picking.
    int cursor = p_code.length();
    int word_end = cursor;
    int word_start = word_end;
    while (word_start > 0) {
        char32_t c = p_code[word_start - 1];
        bool is_ident = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')
                     || (c >= '0' && c <= '9') || c == '_' || c == '$';
        if (!is_ident) break;
        --word_start;
    }
    String qualifier;
    if (word_start > 0 && p_code[word_start - 1] == '.') {
        int q_end = word_start - 1;
        int q_start = q_end;
        while (q_start > 0) {
            char32_t c = p_code[q_start - 1];
            bool is_ident = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')
                         || (c >= '0' && c <= '9') || c == '_';
            if (!is_ident) break;
            --q_start;
        }
        qualifier = p_code.substr(q_start, q_end - q_start).to_upper();
    }
    bool godot_dot_ctx = (qualifier == String("GODOT"));

    // ── GODOT.* native suite (functions) ─────────────────────────
    static const char* godot_natives[] = {
        "GODOT.SELF", "GODOT.GET", "GODOT.SET", "GODOT.CALL", "GODOT.EMIT",
        "GODOT.CONNECT", "GODOT.DISCONNECT", "GODOT.TIMER",
        "GODOT.AUDIO.PLAY", "GODOT.AUDIO.MUSIC", "GODOT.AUDIO.STOP_MUSIC", "GODOT.AUDIO.STOP",
        "GODOT.LOAD", "GODOT.INSTANTIATE", "GODOT.NEW",
        "GODOT.ADD_CHILD", "GODOT.QUEUE_FREE",
        "GODOT.TIME_MS", "GODOT.TIME_SEC",
        "GODOT.VEC2", "GODOT.VEC3", "GODOT.VEC2I", "GODOT.COLOR", "GODOT.RECT2", "GODOT.REF",
        "GODOT.DRAW_TEXT", "GODOT.DRAW_STRING", "GODOT.TEXT_SIZE", "GODOT.PRINT",
    };
    for (const char* n : godot_natives) {
        String full = String(n);
        // After "GODOT." the user already has the prefix; only insert the
        // suffix so the dot doesn't get duplicated.
        String insert = godot_dot_ctx ? full.substr(6, full.length() - 6) : full;
        push_opt(1, full, insert, 0);
    }

    // ── jdBasic top-level + structural keywords ──────────────────
    static const char* keywords[] = {
        "EXTENDS", "INSPECTOR", "SIGNAL",
        "DIM", "CONST", "STATIC", "AS", "EXPORT", "IMPORT", "MODULE",
        "SUB", "ENDSUB", "FUNC", "ENDFUNC", "RETURN",
        "IF", "THEN", "ELSE", "ELSEIF", "ENDIF",
        "FOR", "TO", "STEP", "NEXT", "EACH", "IN",
        "DO", "LOOP", "WHILE", "UNTIL",
        "SWITCH", "CASE", "DEFAULT", "ENDSWITCH",
        "TRY", "CATCH", "FINALLY", "ENDTRY", "THROW",
        "TRUE", "FALSE", "NONE", "NULL", "PI", "E",
        "AND", "OR", "NOT", "ANDALSO", "ORELSE", "MOD",
        "PRINT", "INPUT", "SLEEP",
    };
    for (const char* k : keywords) push_opt(9, String(k), String(k), 0);

    // ── User-defined FUNC / SUB / DIM scan ───────────────────────
    PackedStringArray lines = p_code.split(String("\n"), true);
    for (int i = 0; i < lines.size(); ++i) {
        String line = lines[i].strip_edges();
        String low  = line.to_lower();

        int name_start = -1;
        int kind = 9;
        if (low.begins_with(String("sub ")))           { name_start = 4;  kind = 1; }
        else if (low.begins_with(String("func ")))      { name_start = 5;  kind = 1; }
        else if (low.begins_with(String("dim ")))       { name_start = 4;  kind = 3; }
        else if (low.begins_with(String("inspector dim "))) { name_start = 14; kind = 3; }
        else if (low.begins_with(String("const ")))     { name_start = 6;  kind = 6; }
        else if (low.begins_with(String("signal ")))    { name_start = 7;  kind = 2; }
        else continue;

        while (name_start < line.length()
               && (line[name_start] == ' ' || line[name_start] == '\t')) ++name_start;
        int name_end = name_start;
        while (name_end < line.length()) {
            char32_t c = line[name_end];
            bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')
                   || (c >= '0' && c <= '9') || c == '_' || c == '$';
            if (!ok) break;
            ++name_end;
        }
        if (name_end > name_start) {
            String name = line.substr(name_start, name_end - name_start);
            push_opt(kind, name, name, i + 1);
        }
    }

    d[String("result")]    = Error::OK;
    d[String("force")]     = false;
    d[String("call_hint")] = String();
    d[String("options")]   = options;
    return d;
}

Dictionary JdbScriptLanguage::_lookup_code(const String& p_code,
                                            const String& p_symbol,
                                            const String& p_path,
                                            Object* /*p_owner*/) const {
    // Scan source for SUB / FUNC / DIM / INSPECTOR DIM / CONST / SIGNAL
    // declarations matching p_symbol. Case-insensitive (jdBasic-style).
    Dictionary d;
    d[String("type")]         = 0;  // LOOKUP_RESULT_SCRIPT_LOCATION
    d[String("location")]     = -1;
    d[String("script")]       = Variant();
    d[String("class_name")]   = String();
    d[String("class_path")]   = p_path;
    d[String("class_member")] = p_symbol;
    d[String("description")]  = String();

    String needle = p_symbol.to_lower();
    PackedStringArray lines = p_code.split(String("\n"), true);
    for (int i = 0; i < lines.size(); ++i) {
        String line = lines[i].strip_edges();
        String low  = line.to_lower();

        int name_start = -1;
        if      (low.begins_with(String("sub ")))            name_start = 4;
        else if (low.begins_with(String("func ")))           name_start = 5;
        else if (low.begins_with(String("dim ")))            name_start = 4;
        else if (low.begins_with(String("inspector dim ")))  name_start = 14;
        else if (low.begins_with(String("const ")))          name_start = 6;
        else if (low.begins_with(String("signal ")))         name_start = 7;
        else continue;

        while (name_start < low.length()
               && (low[name_start] == ' ' || low[name_start] == '\t')) ++name_start;
        int name_end = name_start;
        while (name_end < low.length()) {
            char32_t c = low[name_end];
            bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')
                   || c == '_' || c == '$';
            if (!ok) break;
            ++name_end;
        }
        if (name_end > name_start
            && low.substr(name_start, name_end - name_start) == needle) {
            d[String("result")]   = Error::OK;
            d[String("location")] = i + 1;
            return d;
        }
    }

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

// ── T7 debugger virtuals ───────────────────────────────────────────
//
// All of these route to whichever JdbScriptInstance is currently paused
// (set via set_break_instance from JdbScriptInstance::on_debug_break) and
// query its VM through the embed debug ABI.

String JdbScriptLanguage::_debug_get_error() const {
    return String();
}

int32_t JdbScriptLanguage::_debug_get_stack_level_count() const {
    if (!m_break_inst || !m_break_inst->get_vm()) return 1;  // at least the global frame
    int n = jdb_embed_debug_stack_count(m_break_inst->get_vm());
    return n > 0 ? n : 1;
}

int32_t JdbScriptLanguage::_debug_get_stack_level_line(int32_t p_level) const {
    if (!m_break_inst || !m_break_inst->get_vm()) return 0;
    JdbEmbed* vm = m_break_inst->get_vm();
    if (jdb_embed_debug_stack_count(vm) == 0) return jdb_embed_debug_current_line(vm);
    return jdb_embed_debug_stack_line(vm, p_level);
}

String JdbScriptLanguage::_debug_get_stack_level_function(int32_t p_level) const {
    if (!m_break_inst || !m_break_inst->get_vm()) return String("<global>");
    JdbEmbed* vm = m_break_inst->get_vm();
    if (jdb_embed_debug_stack_count(vm) == 0) return String("<global>");
    return String::utf8(jdb_embed_debug_stack_function(vm, p_level));
}

String JdbScriptLanguage::_debug_get_stack_level_source(int32_t /*p_level*/) const {
    if (!m_break_inst) return String();
    Ref<JdbScriptResource> s = m_break_inst->get_script();
    return s.is_valid() ? s->get_path() : String();
}

Dictionary JdbScriptLanguage::_debug_get_stack_level_locals(int32_t /*p_level*/, int32_t /*p_max_subitems*/, int32_t /*p_max_depth*/) {
    // Godot's wrapper expects { "<kind>": [names], "values": [values] }.
    PackedStringArray names;
    Array values;
    if (m_break_inst && m_break_inst->get_vm()) {
        JdbEmbed* vm = m_break_inst->get_vm();
        int n = jdb_embed_debug_locals_count(vm);
        for (int i = 0; i < n; ++i) {
            names.push_back(String::utf8(jdb_embed_debug_local_name(vm, i)));
            values.push_back(String::utf8(jdb_embed_debug_local_value(vm, i)));
        }
    }
    Dictionary d;
    d[String("locals")] = names;
    d[String("values")] = values;
    return d;
}

Dictionary JdbScriptLanguage::_debug_get_stack_level_members(int32_t /*p_level*/, int32_t /*p_max_subitems*/, int32_t /*p_max_depth*/) {
    // jdBasic instance state lives in globals (surfaced below), so members
    // is empty - but still return the expected shape.
    Dictionary d;
    d[String("members")] = PackedStringArray();
    d[String("values")]  = Array();
    return d;
}

void* JdbScriptLanguage::_debug_get_stack_level_instance(int32_t /*p_level*/) {
    return nullptr;  // no Godot Object backs a jdBasic stack frame
}

Dictionary JdbScriptLanguage::_debug_get_globals(int32_t /*p_max_subitems*/, int32_t /*p_max_depth*/) {
    PackedStringArray names;
    Array values;
    if (m_break_inst && m_break_inst->get_vm()) {
        JdbEmbed* vm = m_break_inst->get_vm();
        int n = jdb_embed_debug_globals_count(vm);
        for (int i = 0; i < n; ++i) {
            names.push_back(String::utf8(jdb_embed_debug_global_name(vm, i)));
            values.push_back(String::utf8(jdb_embed_debug_global_value(vm, i)));
        }
    }
    Dictionary d;
    d[String("globals")] = names;
    d[String("values")]  = values;
    return d;
}

String JdbScriptLanguage::_debug_parse_stack_level_expression(int32_t /*p_level*/, const String& /*p_expression*/, int32_t /*p_max_subitems*/, int32_t /*p_max_depth*/) {
    return String();  // watch expressions land in P5
}

TypedArray<Dictionary> JdbScriptLanguage::_debug_get_current_stack_info() {
    TypedArray<Dictionary> out;
    if (!m_break_inst || !m_break_inst->get_vm()) return out;
    JdbEmbed* vm = m_break_inst->get_vm();
    String src;
    {
        Ref<JdbScriptResource> s = m_break_inst->get_script();
        if (s.is_valid()) src = s->get_path();
    }
    int n = jdb_embed_debug_stack_count(vm);
    if (n == 0) {
        Dictionary f;
        f[String("func")] = String("<global>");
        f[String("line")] = jdb_embed_debug_current_line(vm);
        f[String("file")] = src;
        out.push_back(f);
        return out;
    }
    for (int i = 0; i < n; ++i) {
        Dictionary f;
        f[String("func")] = String::utf8(jdb_embed_debug_stack_function(vm, i));
        f[String("line")] = jdb_embed_debug_stack_line(vm, i);
        f[String("file")] = src;
        out.push_back(f);
    }
    return out;
}

#endif  // GODOT

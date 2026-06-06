// JDBScript - see jdb_script.h.

#ifdef GODOT

#include "jdb_script.h"
#include "jdb_embed_api.h"

#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <cstdio>

#ifdef _WIN32
#include <direct.h>
#define jdb_chdir _chdir
#else
#include <unistd.h>
#define jdb_chdir chdir
#endif

using namespace godot;

void JDBScript::_bind_methods() {
    ClassDB::bind_method(D_METHOD("set_script_path", "path"), &JDBScript::set_script_path);
    ClassDB::bind_method(D_METHOD("get_script_path"),         &JDBScript::get_script_path);
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "script_path",
                              PROPERTY_HINT_FILE, "*.jdb"),
                 "set_script_path", "get_script_path");

    ClassDB::bind_method(D_METHOD("call",     "func", "args"), &JDBScript::call);
    ClassDB::bind_method(D_METHOD("call_sub", "sub",  "args"), &JDBScript::call_sub);
    ClassDB::bind_method(D_METHOD("set_var",  "name", "value"),&JDBScript::set_var);
    ClassDB::bind_method(D_METHOD("get_var",  "name"),         &JDBScript::get_var);
    ClassDB::bind_method(D_METHOD("recompile"),                &JDBScript::recompile);
    ClassDB::bind_method(D_METHOD("eval",     "code"),         &JDBScript::eval);
    ClassDB::bind_method(D_METHOD("last_error"),               &JDBScript::last_error);
}

JDBScript::JDBScript() {}

JDBScript::~JDBScript() {
    if (m_vm) {
        jdb_embed_shutdown(m_vm);
        m_vm = nullptr;
    }
}

void JDBScript::set_script_path(const String& p) {
    m_script_path = p;
}

String JDBScript::get_script_path() const {
    return m_script_path;
}

bool JDBScript::scan_for_def_(const String& source, const String& name) {
    // Cheap source scan for `FUNC name` / `SUB name` (case-insensitive,
    // anywhere on a line). The parser does the real validation - we just
    // need a yes/no so the frame loop can short-circuit when the hook
    // isn't defined.
    String lower_src  = source.to_lower();
    String lower_name = name.to_lower();
    int pos = 0;
    while (true) {
        int func_at = lower_src.find("func " + lower_name, pos);
        int sub_at  = lower_src.find("sub "  + lower_name, pos);
        int hit = -1;
        if (func_at >= 0 && (sub_at < 0 || func_at < sub_at)) hit = func_at;
        else if (sub_at >= 0)                                 hit = sub_at;
        if (hit < 0) return false;
        // Make sure the next char after the name is end-of-line, space,
        // paren, or tab - otherwise we matched a longer name by mistake.
        int after = hit + (lower_src[hit] == 'f' ? 5 : 4) + lower_name.length();
        if (after >= lower_src.length()) return true;
        char32_t c = lower_src[after];
        if (c == ' ' || c == '\t' || c == '(' || c == '\n' || c == '\r') return true;
        pos = after;
    }
}

void JDBScript::load_source_() {
    m_last_error = "";
    m_has_ready = m_has_process = m_has_exit = false;

    if (m_script_path.is_empty()) {
        m_last_error = "JDBScript: script_path is empty";
        return;
    }
    String source = FileAccess::get_file_as_string(m_script_path);
    if (source.is_empty()) {
        m_last_error = "JDBScript: could not read " + m_script_path;
        return;
    }
    char* out = jdb_embed_eval(m_vm, source.utf8().get_data());
    if (!out) {
        const char* msg = jdb_embed_last_error(m_vm);
        m_last_error = String("JDBScript: ") + String(msg ? msg : "unknown error during load");
        return;
    }
    jdb_embed_free(out);

    m_has_ready   = scan_for_def_(source, String("on_ready"));
    m_has_process = scan_for_def_(source, String("on_process"));
    m_has_exit    = scan_for_def_(source, String("on_exit"));
}

void JDBScript::_ready() {
    if (Engine::get_singleton() && Engine::get_singleton()->is_editor_hint()) {
        // Don't spin up a VM in the editor (script_path picker etc).
        return;
    }
    m_vm = jdb_embed_init();
    if (!m_vm) {
        m_last_error = "JDBScript: jdb_embed_init() returned NULL";
        return;
    }
    // Resolve IMPORT and relative file I/O (TXTREADER$, SFX.LOAD, ...) against
    // the project directory rather than wherever Godot was launched from.
    if (ProjectSettings::get_singleton()) {
        String proj = ProjectSettings::get_singleton()->globalize_path("res://");
        if (!proj.is_empty()) jdb_chdir(proj.utf8().get_data());
    }
    load_source_();
    if (m_has_ready) {
        char* out = jdb_embed_eval(m_vm, "on_ready()\n");
        if (out) jdb_embed_free(out);
    }
}

void JDBScript::_process(double delta) {
    if (!m_vm || !m_has_process) return;
    // Single eval per frame. The string is short enough that lex+parse+
    // compile is microseconds. E3 will replace this with a typed call.
    char buf[64];
    snprintf(buf, sizeof(buf), "on_process(%.7f)\n", delta);
    char* out = jdb_embed_eval(m_vm, buf);
    if (out) jdb_embed_free(out);
    else {
        const char* msg = jdb_embed_last_error(m_vm);
        if (msg) m_last_error = String("on_process: ") + String(msg);
    }
}

void JDBScript::_exit_tree() {
    if (m_vm && m_has_exit) {
        char* out = jdb_embed_eval(m_vm, "on_exit()\n");
        if (out) jdb_embed_free(out);
    }
}

String JDBScript::format_arg_(const Variant& v) {
    switch (v.get_type()) {
        case Variant::BOOL:
            return String(bool(v) ? "1" : "0");
        case Variant::INT:
            return String::num_int64(int64_t(v));
        case Variant::FLOAT:
            return String::num(double(v), 7);
        case Variant::STRING:
            return String("\"") + String(v).replace(String("\""), String("\\\"")) + String("\"");
        default:
            return String("0");
    }
}

String JDBScript::build_call_(const String& func, const Array& args) {
    String s = func + String("(");
    for (int i = 0; i < args.size(); ++i) {
        if (i > 0) s += String(", ");
        s += format_arg_(args[i]);
    }
    s += String(")");
    return s;
}

Variant JDBScript::call(const String& func, const Array& args) {
    if (!m_vm) return Variant();
    String code = String("PRINT ") + build_call_(func, args) + String("\n");
    char* out = jdb_embed_eval(m_vm, code.utf8().get_data());
    if (!out) {
        const char* msg = jdb_embed_last_error(m_vm);
        if (msg) m_last_error = String("call ") + func + String(": ") + String(msg);
        return Variant();
    }
    String s = String::utf8(out).strip_edges();
    jdb_embed_free(out);
    if (s.is_empty() || s == String("NONE")) return Variant();
    if (s.is_valid_float()) return s.to_float();
    return s;
}

void JDBScript::call_sub(const String& sub, const Array& args) {
    if (!m_vm) return;
    String code = build_call_(sub, args) + String("\n");
    char* out = jdb_embed_eval(m_vm, code.utf8().get_data());
    if (out) jdb_embed_free(out);
    else {
        const char* msg = jdb_embed_last_error(m_vm);
        if (msg) m_last_error = String("call_sub ") + sub + String(": ") + String(msg);
    }
}

void JDBScript::set_var(const String& name, const Variant& value) {
    if (!m_vm) return;
    String code = name + String(" = ") + format_arg_(value) + String("\n");
    char* out = jdb_embed_eval(m_vm, code.utf8().get_data());
    if (out) jdb_embed_free(out);
    else {
        const char* msg = jdb_embed_last_error(m_vm);
        if (msg) m_last_error = String("set_var ") + name + String(": ") + String(msg);
    }
}

Variant JDBScript::get_var(const String& name) {
    if (!m_vm) return Variant();
    String code = String("PRINT ") + name + String("\n");
    char* out = jdb_embed_eval(m_vm, code.utf8().get_data());
    if (!out) {
        const char* msg = jdb_embed_last_error(m_vm);
        if (msg) m_last_error = String("get_var ") + name + String(": ") + String(msg);
        return Variant();
    }
    String s = String::utf8(out).strip_edges();
    jdb_embed_free(out);
    if (s.is_empty() || s == String("NONE")) return Variant();
    if (s.is_valid_float()) return s.to_float();
    return s;
}

String JDBScript::recompile() {
    if (!m_vm) return String();
    if (m_script_path.is_empty()) {
        m_last_error = "JDBScript: script_path is empty";
        return String();
    }
    // Re-read via FileAccess (res:// works); pass the source string to the
    // C-ABI so we don't rely on the embed side knowing about Godot's vfs.
    String source = FileAccess::get_file_as_string(m_script_path);
    if (source.is_empty()) {
        m_last_error = "JDBScript: could not re-read " + m_script_path;
        return String();
    }
    char* out = jdb_embed_recompile_source(m_vm, source.utf8().get_data());
    if (!out) {
        const char* msg = jdb_embed_last_error(m_vm);
        m_last_error = String("recompile: ") + String(msg ? msg : "unknown error");
        return String();
    }
    String summary = String::utf8(out);
    jdb_embed_free(out);
    m_has_ready   = scan_for_def_(source, String("on_ready"));
    m_has_process = scan_for_def_(source, String("on_process"));
    m_has_exit    = scan_for_def_(source, String("on_exit"));
    return summary;
}

String JDBScript::eval(const String& code) {
    if (!m_vm) return String();
    char* out = jdb_embed_eval(m_vm, code.utf8().get_data());
    if (!out) {
        const char* msg = jdb_embed_last_error(m_vm);
        if (msg) m_last_error = String(msg);
        return String();
    }
    String s = String::utf8(out);
    jdb_embed_free(out);
    return s;
}

String JDBScript::last_error() const {
    return m_last_error;
}

#endif  // GODOT

// JdbScriptResource - T3.0 implementation. See header.

#ifdef GODOT

#include "jdb_script_resource.h"
#include "jdb_script_language.h"
#include "jdb_script_instance.h"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/gdextension_interface_loader.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/variant.hpp>

using namespace godot;

void JdbScriptResource::_bind_methods() {}

JdbScriptResource::JdbScriptResource() : m_extends_type(StringName("Node")) {}
JdbScriptResource::~JdbScriptResource() {}

// ── Path-B preprocessing ────────────────────────────────────────────
//
// We scan the source line by line. Two pieces of jdBasic-flavoured
// syntax that the runtime VM does not understand:
//
//   EXTENDS Node3D           -> commented out, base type captured
//   INSPECTOR DIM speed = 1  -> INSPECTOR keyword stripped, name+default captured
//
// Everything else passes through untouched. The processed source is what
// gets handed to jdb_embed_eval; the original is what _get_source_code
// returns so Godot's editor sees what the user typed.

namespace {

// Trim leading whitespace and return the offset of the first non-ws byte.
int leading_ws(const String& s) {
    for (int i = 0; i < s.length(); ++i) {
        char32_t c = s[i];
        if (c != ' ' && c != '\t') return i;
    }
    return s.length();
}

bool starts_with_keyword_ci(const String& trimmed, const char* kw, int kw_len) {
    if (trimmed.length() < kw_len + 1) return false;
    for (int i = 0; i < kw_len; ++i) {
        char32_t c = trimmed[i];
        if (c >= 'a' && c <= 'z') c -= 32;
        if (c != (char32_t)kw[i]) return false;
    }
    char32_t after = trimmed[kw_len];
    return after == ' ' || after == '\t';
}

// Parse "name = value" — minimal. Returns true on success and fills name + default_str.
bool parse_dim_assign(const String& tail, String& out_name, String& out_default) {
    int eq = tail.find(String("="));
    if (eq < 0) return false;
    String left  = tail.substr(0, eq).strip_edges();
    String right = tail.substr(eq + 1).strip_edges();
    if (left.is_empty() || right.is_empty()) return false;
    out_name    = left;
    out_default = right;
    return true;
}

// Crude type inference for the default value.
// Float literal -> FLOAT, integer literal -> INT, quoted -> STRING.
Variant default_value_from_literal(const String& s, Variant::Type& out_type) {
    if (s.length() >= 2 && s[0] == '"' && s[s.length() - 1] == '"') {
        out_type = Variant::STRING;
        return s.substr(1, s.length() - 2);
    }
    if (s.is_valid_int()) {
        out_type = Variant::INT;
        return s.to_int();
    }
    if (s.is_valid_float()) {
        out_type = Variant::FLOAT;
        return s.to_float();
    }
    // Fallback - treat as string literal.
    out_type = Variant::STRING;
    return s;
}

}  // namespace

void JdbScriptResource::preprocess_() {
    m_source_processed = String();
    m_extends_type     = StringName("Node");
    m_inspector_vars   = TypedArray<Dictionary>();

    PackedStringArray lines = m_source.split(String("\n"), true);
    String out;
    out.resize(m_source.length() + 32);  // hint; not strictly needed
    out = String();

    bool seen_extends = false;
    for (int i = 0; i < lines.size(); ++i) {
        String line = lines[i];
        int    pad  = leading_ws(line);
        String trim = line.substr(pad, line.length() - pad);

        // EXTENDS Foo -- first one wins; subsequent ones are passed through
        // unchanged (they'd be syntax errors in the jdBasic eval anyway).
        if (!seen_extends && starts_with_keyword_ci(trim, "EXTENDS", 7)) {
            String tail = trim.substr(7, trim.length() - 7).strip_edges();
            if (!tail.is_empty()) {
                m_extends_type = StringName(tail);
                seen_extends   = true;
                out += line.substr(0, pad) + String("' ") + trim;
                if (i + 1 < lines.size()) out += String("\n");
                continue;
            }
        }

        // INSPECTOR DIM name = value
        if (starts_with_keyword_ci(trim, "INSPECTOR", 9)) {
            String after_insp = trim.substr(9, trim.length() - 9).strip_edges();
            if (starts_with_keyword_ci(after_insp, "DIM", 3)) {
                String tail = after_insp.substr(3, after_insp.length() - 3).strip_edges();
                String var_name, default_str;
                if (parse_dim_assign(tail, var_name, default_str)) {
                    Variant::Type t;
                    Variant dv = default_value_from_literal(default_str, t);
                    Dictionary d;
                    d[String("name")]    = var_name;
                    d[String("default")] = dv;
                    d[String("type")]    = (int)t;
                    m_inspector_vars.append(d);
                    // Strip "INSPECTOR " from the line so jdBasic eats it
                    // as a normal DIM.
                    out += line.substr(0, pad) + after_insp;
                    if (i + 1 < lines.size()) out += String("\n");
                    continue;
                }
            }
        }

        out += line;
        if (i + 1 < lines.size()) out += String("\n");
    }
    m_source_processed = out;
}

bool JdbScriptResource::_has_source_code() const {
    return !m_source.is_empty();
}

String JdbScriptResource::_get_source_code() const {
    return m_source;
}

void JdbScriptResource::_set_source_code(const String& p_code) {
    m_source = p_code;
    preprocess_();
}

bool JdbScriptResource::_can_instantiate() const {
    // T3.0: lying yes so the editor's "Attach Script" dialog allows
    // selecting our language. T3.2 hooks real instance creation.
    return true;
}

bool JdbScriptResource::_is_valid() const {
    return true;
}

bool JdbScriptResource::_is_tool() const {
    return false;
}

StringName JdbScriptResource::_get_instance_base_type() const {
    return m_extends_type;
}

bool JdbScriptResource::_has_method(const StringName& /*method*/) const {
    // T3.0 stub - reports no methods. T3.2 scans the source for FUNC/SUB
    // definitions.
    return false;
}

bool JdbScriptResource::_has_property_default_value(const StringName& p_property) const {
    String name = p_property;
    for (int i = 0; i < m_inspector_vars.size(); ++i) {
        Dictionary d = m_inspector_vars[i];
        if (String(d[String("name")]) == name) return true;
    }
    return false;
}

Variant JdbScriptResource::_get_property_default_value(const StringName& p_property) const {
    String name = p_property;
    for (int i = 0; i < m_inspector_vars.size(); ++i) {
        Dictionary d = m_inspector_vars[i];
        if (String(d[String("name")]) == name) return d[String("default")];
    }
    return Variant();
}

TypedArray<Dictionary> JdbScriptResource::_get_script_property_list() const {
    // Re-shape m_inspector_vars (which has name/default/type) into the
    // PropertyInfo dictionaries Godot expects in script-property lists.
    TypedArray<Dictionary> out;
    static const String K_NAME       = String("name");
    static const String K_TYPE       = String("type");
    static const String K_CLASS_NAME = String("class_name");
    static const String K_HINT       = String("hint");
    static const String K_HINT_STR   = String("hint_string");
    static const String K_USAGE      = String("usage");
    static const String K_DEFAULT    = String("default");
    for (int i = 0; i < m_inspector_vars.size(); ++i) {
        Dictionary src = m_inspector_vars[i];
        Dictionary p;
        p[K_NAME]       = src[K_NAME];
        p[K_TYPE]       = src[K_TYPE];
        p[K_CLASS_NAME] = String();
        p[K_HINT]       = 0;
        p[K_HINT_STR]   = String();
        p[K_USAGE]      = 6;  // PROPERTY_USAGE_STORAGE | EDITOR
        out.append(p);
    }
    return out;
}

Error JdbScriptResource::_reload(bool /*p_keep_state*/) {
    // T3.0 no-op. T3.4 plugs this into jdb_embed_recompile_source.
    return Error::OK;
}

ScriptLanguage* JdbScriptResource::_get_language() const {
    return JdbScriptLanguage::get_singleton();
}

TypedArray<Dictionary> JdbScriptResource::_get_documentation() const {
    return TypedArray<Dictionary>();
}

void* JdbScriptResource::_instance_create(Object* p_for_object) const {
    // Const cast: Godot's API hands us a const ScriptExtension*; building
    // a Ref<> needs a non-const pointer. The instance only borrows the
    // script reference, never mutates the script object itself.
    Ref<JdbScriptResource> script(const_cast<JdbScriptResource*>(this));
    JdbScriptInstance* inst = new JdbScriptInstance(script, p_for_object);
    GDExtensionScriptInstancePtr h =
        gdextension_interface::script_instance_create3(&JdbScriptInstance::s_info, inst);
    inst->set_godot_handle(h);
    return h;
}

#endif  // GODOT

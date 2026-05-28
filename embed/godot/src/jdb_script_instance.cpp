// JdbScriptInstance - per-Node attached-script runtime. See header.

#ifdef GODOT

#include "jdb_script_instance.h"
#include "jdb_script_resource.h"
#include "jdb_script_language.h"
#include "jdb_embed_api.h"

#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/classes/script_language.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <cstdio>

using namespace godot;

// ── Helpers ────────────────────────────────────────────────────────

std::string JdbScriptInstance::variant_to_jdb_arg_(const Variant& v) {
    switch (v.get_type()) {
        case Variant::BOOL: {
            return bool(v) ? "1" : "0";
        }
        case Variant::INT: {
            char buf[32];
            snprintf(buf, sizeof(buf), "%lld", (long long)int64_t(v));
            return std::string(buf);
        }
        case Variant::FLOAT: {
            char buf[40];
            snprintf(buf, sizeof(buf), "%.7f", (double)v);
            return std::string(buf);
        }
        case Variant::STRING: {
            std::string raw = String(v).utf8().get_data();
            std::string out = "\"";
            for (char c : raw) {
                if (c == '"') out += "\\\"";
                else out += c;
            }
            out += "\"";
            return out;
        }
        default:
            return "0";  // unsupported types degrade to numeric zero
    }
}

void JdbScriptInstance::scan_methods_(const String& source) {
    // Simple FUNC / SUB regex-free scan. Look for line-starts (after
    // optional whitespace) matching "FUNC name" or "SUB name". The
    // jdBasic parser will give the authoritative answer at eval time;
    // this set is only the "is the method present?" hint Godot needs
    // for has_method dispatch.
    std::string s = source.utf8().get_data();
    size_t i = 0;
    while (i < s.size()) {
        size_t line_start = i;
        while (line_start < s.size() && (s[line_start] == ' ' || s[line_start] == '\t')) ++line_start;
        size_t line_end = s.find('\n', line_start);
        if (line_end == std::string::npos) line_end = s.size();

        // Match "FUNC " or "SUB " (case-insensitive) at line_start.
        auto match_kw = [&](const char* kw, size_t kw_len) -> bool {
            if (line_start + kw_len + 1 > line_end) return false;
            for (size_t k = 0; k < kw_len; ++k) {
                char c = s[line_start + k];
                char up = (c >= 'a' && c <= 'z') ? (char)(c - 32) : c;
                if (up != kw[k]) return false;
            }
            char after = s[line_start + kw_len];
            return after == ' ' || after == '\t';
        };

        size_t name_start = 0;
        if (match_kw("FUNC", 4)) name_start = line_start + 5;
        else if (match_kw("SUB", 3)) name_start = line_start + 4;
        if (name_start > 0) {
            // Skip extra whitespace after the keyword.
            while (name_start < line_end && (s[name_start] == ' ' || s[name_start] == '\t')) ++name_start;
            size_t name_end = name_start;
            while (name_end < line_end) {
                char c = s[name_end];
                bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')
                       || (c >= '0' && c <= '9') || c == '_';
                if (!ok) break;
                ++name_end;
            }
            if (name_end > name_start) {
                std::string name(s.begin() + name_start, s.begin() + name_end);
                std::transform(name.begin(), name.end(), name.begin(),
                               [](unsigned char c){ return (char)std::tolower(c); });
                m_method_set.insert(std::move(name));
            }
        }

        i = (line_end == s.size()) ? line_end : line_end + 1;
    }
}

// ── Lifecycle ──────────────────────────────────────────────────────

JdbScriptInstance::JdbScriptInstance(Ref<JdbScriptResource> p_script, Object* p_owner)
    : m_script(p_script), m_owner(p_owner) {
    m_vm = jdb_embed_init();
    if (!m_vm) return;
    if (m_script.is_valid()) {
        // Load the preprocessed source - that's what jdBasic can actually
        // eval (EXTENDS / INSPECTOR have been stripped out into metadata).
        String src = m_script->get_processed_source();
        if (src.is_empty()) src = m_script->_get_source_code();
        if (!src.is_empty()) {
            char* out = jdb_embed_eval(m_vm, src.utf8().get_data());
            if (out) jdb_embed_free(out);
        }
        scan_methods_(m_script->_get_source_code());
    }
}

JdbScriptInstance::~JdbScriptInstance() {
    if (m_vm) {
        jdb_embed_shutdown(m_vm);
        m_vm = nullptr;
    }
}

bool JdbScriptInstance::has_method(const StringName& name) const {
    std::string n = String(name).utf8().get_data();
    std::transform(n.begin(), n.end(), n.begin(),
                   [](unsigned char c){ return (char)std::tolower(c); });
    return m_method_set.find(n) != m_method_set.end();
}

Variant JdbScriptInstance::call_method(const StringName& name,
                                       const Variant** args, int64_t argc) {
    if (!m_vm) return Variant();
    std::string n = String(name).utf8().get_data();
    std::string code;
    code.reserve(64 + argc * 16);
    code += "PRINT ";
    code += n;
    code += "(";
    for (int64_t i = 0; i < argc; ++i) {
        if (i > 0) code += ", ";
        code += variant_to_jdb_arg_(*args[i]);
    }
    code += ")\n";
    char* out = jdb_embed_eval(m_vm, code.c_str());
    if (!out) return Variant();
    String s = String::utf8(out).strip_edges();
    jdb_embed_free(out);
    if (s.is_empty() || s == String("NONE")) return Variant();
    if (s.is_valid_float()) return s.to_float();
    return s;
}

// ── C bouncers for GDExtensionScriptInstanceInfo3 ────────────────────
//
// Each callback receives a void* that we constructed in _instance_create
// and handed to script_instance_create3. We downcast it back and forward
// to the C++ method.

namespace {

JdbScriptInstance* self_of(GDExtensionScriptInstanceDataPtr p) {
    return reinterpret_cast<JdbScriptInstance*>(p);
}

GDExtensionBool bounce_set(GDExtensionScriptInstanceDataPtr /*p_instance*/,
                            GDExtensionConstStringNamePtr /*p_name*/,
                            GDExtensionConstVariantPtr /*p_value*/) {
    return 0;  // T3.3 will route to jdBasic globals via INSPECTOR DIM list.
}

GDExtensionBool bounce_get(GDExtensionScriptInstanceDataPtr /*p_instance*/,
                            GDExtensionConstStringNamePtr /*p_name*/,
                            GDExtensionVariantPtr /*r_ret*/) {
    return 0;
}

const GDExtensionPropertyInfo* bounce_get_property_list(
        GDExtensionScriptInstanceDataPtr /*p_instance*/,
        uint32_t* r_count) {
    *r_count = 0;
    return nullptr;
}

void bounce_free_property_list(GDExtensionScriptInstanceDataPtr /*p_instance*/,
                                const GDExtensionPropertyInfo* /*p_list*/,
                                uint32_t /*p_count*/) {}

GDExtensionBool bounce_property_can_revert(GDExtensionScriptInstanceDataPtr /*p*/,
                                            GDExtensionConstStringNamePtr /*n*/) {
    return 0;
}

GDExtensionBool bounce_property_get_revert(GDExtensionScriptInstanceDataPtr /*p*/,
                                            GDExtensionConstStringNamePtr /*n*/,
                                            GDExtensionVariantPtr /*r_ret*/) {
    return 0;
}

GDExtensionObjectPtr bounce_get_owner(GDExtensionScriptInstanceDataPtr p) {
    JdbScriptInstance* inst = self_of(p);
    Object* o = inst ? inst->get_owner() : nullptr;
    // godot-cpp's Object has _owner as the engine-side ptr.
    return o ? reinterpret_cast<GDExtensionObjectPtr>(o->_owner) : nullptr;
}

void bounce_get_property_state(GDExtensionScriptInstanceDataPtr /*p*/,
                                GDExtensionScriptInstancePropertyStateAdd /*p_add*/,
                                void* /*p_userdata*/) {}

const GDExtensionMethodInfo* bounce_get_method_list(
        GDExtensionScriptInstanceDataPtr /*p*/,
        uint32_t* r_count) {
    *r_count = 0;
    return nullptr;
}

void bounce_free_method_list(GDExtensionScriptInstanceDataPtr /*p*/,
                              const GDExtensionMethodInfo* /*list*/,
                              uint32_t /*count*/) {}

GDExtensionVariantType bounce_get_property_type(GDExtensionScriptInstanceDataPtr /*p*/,
                                                  GDExtensionConstStringNamePtr /*name*/,
                                                  GDExtensionBool* r_is_valid) {
    if (r_is_valid) *r_is_valid = 0;
    return GDEXTENSION_VARIANT_TYPE_NIL;
}

GDExtensionBool bounce_validate_property(GDExtensionScriptInstanceDataPtr /*p*/,
                                          GDExtensionPropertyInfo* /*p_property*/) {
    return 0;
}

GDExtensionBool bounce_has_method(GDExtensionScriptInstanceDataPtr p,
                                   GDExtensionConstStringNamePtr p_name) {
    JdbScriptInstance* inst = self_of(p);
    if (!inst || !p_name) return 0;
    const StringName& sn = *reinterpret_cast<const StringName*>(p_name);
    return inst->has_method(sn) ? 1 : 0;
}

GDExtensionInt bounce_get_method_argument_count(GDExtensionScriptInstanceDataPtr /*p*/,
                                                  GDExtensionConstStringNamePtr /*p_name*/,
                                                  GDExtensionBool* r_is_valid) {
    if (r_is_valid) *r_is_valid = 0;
    return 0;
}

void bounce_call(GDExtensionScriptInstanceDataPtr p_self,
                  GDExtensionConstStringNamePtr p_method,
                  const GDExtensionConstVariantPtr* p_args,
                  GDExtensionInt p_argc,
                  GDExtensionVariantPtr r_return,
                  GDExtensionCallError* r_error) {
    JdbScriptInstance* inst = self_of(p_self);
    if (!inst || !p_method) {
        if (r_error) r_error->error = GDEXTENSION_CALL_ERROR_INVALID_METHOD;
        return;
    }
    const StringName& name = *reinterpret_cast<const StringName*>(p_method);
    if (!inst->has_method(name)) {
        if (r_error) r_error->error = GDEXTENSION_CALL_ERROR_INVALID_METHOD;
        return;
    }
    // Unpack Variants for our C++-side call_method. GDExtensionConstVariantPtr
    // is `const void*`; cast through the const-array decay manually.
    Variant result = inst->call_method(
        name,
        reinterpret_cast<const Variant**>(const_cast<GDExtensionConstVariantPtr*>(p_args)),
        (int64_t)p_argc);
    if (r_return) {
        *reinterpret_cast<Variant*>(r_return) = result;
    }
    if (r_error) r_error->error = GDEXTENSION_CALL_OK;
}

void bounce_notification(GDExtensionScriptInstanceDataPtr /*p*/,
                          int32_t /*what*/, GDExtensionBool /*reversed*/) {}

void bounce_to_string(GDExtensionScriptInstanceDataPtr /*p*/,
                       GDExtensionBool* r_is_valid, GDExtensionStringPtr /*r_out*/) {
    if (r_is_valid) *r_is_valid = 0;
}

void bounce_refcount_incremented(GDExtensionScriptInstanceDataPtr /*p*/) {}
GDExtensionBool bounce_refcount_decremented(GDExtensionScriptInstanceDataPtr /*p*/) { return 0; }

GDExtensionObjectPtr bounce_get_script(GDExtensionScriptInstanceDataPtr p) {
    JdbScriptInstance* inst = self_of(p);
    if (!inst) return nullptr;
    Ref<JdbScriptResource> s = inst->get_script();
    return s.is_valid() ? reinterpret_cast<GDExtensionObjectPtr>(s->_owner) : nullptr;
}

GDExtensionBool bounce_is_placeholder(GDExtensionScriptInstanceDataPtr /*p*/) { return 0; }

GDExtensionScriptLanguagePtr bounce_get_language(GDExtensionScriptInstanceDataPtr /*p*/) {
    JdbScriptLanguage* lang = JdbScriptLanguage::get_singleton();
    return lang ? reinterpret_cast<GDExtensionScriptLanguagePtr>(lang->_owner) : nullptr;
}

void bounce_free(GDExtensionScriptInstanceDataPtr p) {
    delete self_of(p);
}

}  // namespace

const GDExtensionScriptInstanceInfo3 JdbScriptInstance::s_info = {
    /* set_func                       */ bounce_set,
    /* get_func                       */ bounce_get,
    /* get_property_list_func         */ bounce_get_property_list,
    /* free_property_list_func        */ bounce_free_property_list,
    /* get_class_category_func        */ nullptr,
    /* property_can_revert_func       */ bounce_property_can_revert,
    /* property_get_revert_func       */ bounce_property_get_revert,
    /* get_owner_func                 */ bounce_get_owner,
    /* get_property_state_func        */ bounce_get_property_state,
    /* get_method_list_func           */ bounce_get_method_list,
    /* free_method_list_func          */ bounce_free_method_list,
    /* get_property_type_func         */ bounce_get_property_type,
    /* validate_property_func         */ bounce_validate_property,
    /* has_method_func                */ bounce_has_method,
    /* get_method_argument_count_func */ bounce_get_method_argument_count,
    /* call_func                      */ bounce_call,
    /* notification_func              */ bounce_notification,
    /* to_string_func                 */ bounce_to_string,
    /* refcount_incremented_func      */ bounce_refcount_incremented,
    /* refcount_decremented_func      */ bounce_refcount_decremented,
    /* get_script_func                */ bounce_get_script,
    /* is_placeholder_func            */ bounce_is_placeholder,
    /* set_fallback_func              */ nullptr,
    /* get_fallback_func              */ nullptr,
    /* get_language_func              */ bounce_get_language,
    /* free_func                      */ bounce_free,
};

#endif  // GODOT

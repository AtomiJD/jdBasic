// JDBasicVM implementation. See jdbasic_vm.h for usage.

#ifdef GODOT

#include "jdbasic_vm.h"
#include "jdb_embed_api.h"
#include "jdb_script_instance.h"
#include "jdb_script_resource.h"

#include <godot_cpp/core/class_db.hpp>

using namespace godot;

void JDBasicVM::_bind_methods() {
    ClassDB::bind_method(D_METHOD("eval", "code"),                      &JDBasicVM::eval);
    ClassDB::bind_method(D_METHOD("load", "path"),                      &JDBasicVM::load);
    ClassDB::bind_method(D_METHOD("recompile_source", "source"),        &JDBasicVM::recompile_source);
    ClassDB::bind_method(D_METHOD("recompile", "path"),                 &JDBasicVM::recompile);
    ClassDB::bind_method(D_METHOD("eval_expr", "expr"),                 &JDBasicVM::eval_expr);
    ClassDB::bind_method(D_METHOD("get_var",   "name"),                 &JDBasicVM::get_var);
    ClassDB::bind_method(D_METHOD("last_error"),                        &JDBasicVM::last_error);
    ClassDB::bind_method(D_METHOD("push_input_event", "kind", "action", "type", "strength"),
                                                                        &JDBasicVM::push_input_event);
    ClassDB::bind_method(D_METHOD("clear_input_events"),                &JDBasicVM::clear_input_events);
    ClassDB::bind_method(D_METHOD("pending_input_events"),              &JDBasicVM::pending_input_events);
}

JDBasicVM::JDBasicVM() : m_vm(jdb_embed_init()) {
    if (m_vm) {
        register_godot_input_natives(m_vm, &m_input_queue);
    }
}

void JDBasicVM::push_input_event(const String& kind, const String& action,
                                 const String& type, double strength) {
    InputEventRecord rec;
    rec.kind     = kind;
    rec.action   = action;
    rec.type     = type;
    rec.strength = strength;
    m_input_queue.push(rec);
}

void JDBasicVM::clear_input_events() {
    m_input_queue.clear();
}

int JDBasicVM::pending_input_events() const {
    return static_cast<int>(m_input_queue.size());
}

JDBasicVM::~JDBasicVM() {
    if (m_vm) {
        jdb_embed_shutdown(m_vm);
        m_vm = nullptr;
    }
}

String JDBasicVM::eval(const String& code) {
    if (!m_vm) return String();
    char* out = jdb_embed_eval(m_vm, code.utf8().get_data());
    if (!out) {
        const char* err = jdb_embed_last_error(m_vm);
        return String("[jdb error] ") + (err ? String::utf8(err) : String("unknown"));
    }
    String result = String::utf8(out);
    jdb_embed_free(out);
    return result;
}

String JDBasicVM::load(const String& path) {
    if (!m_vm) return String();
    char* out = jdb_embed_load(m_vm, path.utf8().get_data());
    if (!out) return String();
    String result = String::utf8(out);
    jdb_embed_free(out);
    return result;
}

String JDBasicVM::recompile_source(const String& source) {
    if (!m_vm) return String();
    char* out = jdb_embed_recompile_source(m_vm, source.utf8().get_data());
    if (!out) return String();
    String result = String::utf8(out);
    jdb_embed_free(out);
    return result;
}

String JDBasicVM::recompile(const String& path) {
    if (!m_vm) return String();
    char* out = jdb_embed_recompile(m_vm, path.utf8().get_data());
    if (!out) return String();
    String result = String::utf8(out);
    jdb_embed_free(out);
    return result;
}

Variant JDBasicVM::eval_expr(const String& expr) {
    if (!m_vm) return Variant();
    int64_t h = jdb_embed_eval_expr(m_vm, expr.utf8().get_data());
    if (!h) {
        const char* err = jdb_embed_last_error(m_vm);
        if (err && *err) {
            return String("[jdb error] ") + String::utf8(err);
        }
        return Variant();
    }
    Variant result = JdbScriptInstance::value_to_variant(m_vm, h);
    jdb_embed_value_release(m_vm, h);
    return result;
}

Variant JDBasicVM::get_var(const String& name) {
    if (!m_vm) return Variant();
    int64_t h = jdb_embed_get_global(m_vm, name.utf8().get_data());
    if (!h) return Variant();
    Variant result = JdbScriptInstance::value_to_variant(m_vm, h);
    jdb_embed_value_release(m_vm, h);
    return result;
}

String JDBasicVM::last_error() const {
    if (!m_vm) return String();
    const char* msg = jdb_embed_last_error(m_vm);
    return msg ? String::utf8(msg) : String();
}

#endif  // GODOT

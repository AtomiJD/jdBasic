// JDBasicVM implementation. See jdbasic_vm.h for usage.

#ifdef GODOT

#include "jdbasic_vm.h"
#include "jdb_embed_api.h"

#include <godot_cpp/core/class_db.hpp>

using namespace godot;

void JDBasicVM::_bind_methods() {
    ClassDB::bind_method(D_METHOD("eval", "code"),  &JDBasicVM::eval);
    ClassDB::bind_method(D_METHOD("load", "path"),  &JDBasicVM::load);
    ClassDB::bind_method(D_METHOD("last_error"),    &JDBasicVM::last_error);
}

JDBasicVM::JDBasicVM() : m_vm(jdb_embed_init()) {}

JDBasicVM::~JDBasicVM() {
    if (m_vm) {
        jdb_embed_shutdown(m_vm);
        m_vm = nullptr;
    }
}

String JDBasicVM::eval(const String& code) {
    if (!m_vm) return String();
    char* out = jdb_embed_eval(m_vm, code.utf8().get_data());
    if (!out) return String();
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

String JDBasicVM::last_error() const {
    if (!m_vm) return String();
    const char* msg = jdb_embed_last_error(m_vm);
    return msg ? String::utf8(msg) : String();
}

#endif  // GODOT

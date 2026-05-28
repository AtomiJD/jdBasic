// JdbScriptResource - T3.0 implementation. See header.

#ifdef GODOT

#include "jdb_script_resource.h"
#include "jdb_script_language.h"

#include <godot_cpp/core/class_db.hpp>

using namespace godot;

void JdbScriptResource::_bind_methods() {}

JdbScriptResource::JdbScriptResource() {}
JdbScriptResource::~JdbScriptResource() {}

bool JdbScriptResource::_has_source_code() const {
    return !m_source.is_empty();
}

String JdbScriptResource::_get_source_code() const {
    return m_source;
}

void JdbScriptResource::_set_source_code(const String& p_code) {
    m_source = p_code;
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
    // T3.0 default. T3.1 will parse `EXTENDS Foo` out of the source.
    return StringName("Node");
}

bool JdbScriptResource::_has_method(const StringName& /*method*/) const {
    // T3.0 stub - reports no methods. T3.2 scans the source for FUNC/SUB
    // definitions.
    return false;
}

bool JdbScriptResource::_has_property_default_value(const StringName& /*p_property*/) const {
    return false;
}

Error JdbScriptResource::_reload(bool /*p_keep_state*/) {
    // T3.0 no-op. T3.4 plugs this into jdb_embed_recompile_source.
    return Error::OK;
}

ScriptLanguage* JdbScriptResource::_get_language() const {
    return JdbScriptLanguage::get_singleton();
}

#endif  // GODOT

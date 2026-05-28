// jdb_resource_format - see header.

#ifdef GODOT

#include "jdb_resource_format.h"
#include "jdb_script_resource.h"

#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/core/class_db.hpp>

using namespace godot;

// ── Loader ──────────────────────────────────────────────────────────

void JdbResourceFormatLoader::_bind_methods() {}

PackedStringArray JdbResourceFormatLoader::_get_recognized_extensions() const {
    PackedStringArray r;
    r.push_back(String("jdb"));
    return r;
}

bool JdbResourceFormatLoader::_handles_type(const StringName& p_type) const {
    String t = p_type;
    return t == String("Script") || t == String("ScriptExtension")
        || t == String("JdbScriptResource");
}

String JdbResourceFormatLoader::_get_resource_type(const String& p_path) const {
    if (p_path.get_extension().to_lower() == String("jdb")) {
        return String("JdbScriptResource");
    }
    return String();
}

Variant JdbResourceFormatLoader::_load(const String& p_path,
                                        const String& /*p_original_path*/,
                                        bool /*p_use_sub_threads*/,
                                        int32_t /*p_cache_mode*/) const {
    String src = FileAccess::get_file_as_string(p_path);
    Ref<JdbScriptResource> script;
    script.instantiate();
    script->_set_source_code(src);
    return script;
}

// ── Saver ───────────────────────────────────────────────────────────

void JdbResourceFormatSaver::_bind_methods() {}

Error JdbResourceFormatSaver::_save(const Ref<Resource>& p_resource,
                                    const String& p_path,
                                    uint32_t /*p_flags*/) {
    Ref<JdbScriptResource> script = p_resource;
    if (!script.is_valid()) return Error::ERR_INVALID_PARAMETER;

    Ref<FileAccess> fa = FileAccess::open(p_path, FileAccess::WRITE);
    if (!fa.is_valid()) return Error::ERR_CANT_OPEN;
    fa->store_string(script->_get_source_code());
    fa->close();
    return Error::OK;
}

bool JdbResourceFormatSaver::_recognize(const Ref<Resource>& p_resource) const {
    Ref<JdbScriptResource> script = p_resource;
    return script.is_valid();
}

PackedStringArray JdbResourceFormatSaver::_get_recognized_extensions(
        const Ref<Resource>& p_resource) const {
    PackedStringArray r;
    Ref<JdbScriptResource> script = p_resource;
    if (script.is_valid()) r.push_back(String("jdb"));
    return r;
}

#endif  // GODOT

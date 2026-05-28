// ResourceFormatLoader + Saver for .jdb files.
//
// Without these, ResourceSaver::save() / ResourceLoader::load() have no
// idea how to read or write a JdbScriptResource to/from disk - and the
// Create-Script dialog throws "Could not create script in filesystem".
//
// The loader reads the file as plain UTF-8 text into a new
// JdbScriptResource via _set_source_code (which triggers Path-B
// preprocessing). The saver dumps _get_source_code() (the original user
// text, not the rewritten one).

#pragma once

#ifdef GODOT

#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/classes/resource_format_loader.hpp>
#include <godot_cpp/classes/resource_format_saver.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/string_name.hpp>
#include <godot_cpp/variant/variant.hpp>

namespace godot {

class JdbResourceFormatLoader : public ResourceFormatLoader {
    GDCLASS(JdbResourceFormatLoader, ResourceFormatLoader)
protected:
    static void _bind_methods();
public:
    PackedStringArray _get_recognized_extensions()       const override;
    bool              _handles_type(const StringName& p_type) const override;
    String            _get_resource_type(const String& p_path) const override;
    Variant           _load(const String& p_path, const String& p_original_path,
                            bool p_use_sub_threads, int32_t p_cache_mode) const override;
};

class JdbResourceFormatSaver : public ResourceFormatSaver {
    GDCLASS(JdbResourceFormatSaver, ResourceFormatSaver)
protected:
    static void _bind_methods();
public:
    Error             _save(const Ref<Resource>& p_resource,
                            const String& p_path, uint32_t p_flags) override;
    bool              _recognize(const Ref<Resource>& p_resource) const override;
    PackedStringArray _get_recognized_extensions(const Ref<Resource>& p_resource) const override;
};

}  // namespace godot

#endif  // GODOT

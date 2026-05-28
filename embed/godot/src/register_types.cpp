// GDExtension entry point. Godot calls jdb_godot_library_init when it
// loads addons/jdb_godot/bin/jdb_godot*.dll; we register JDBasicVM with
// the engine's ClassDB at SCENE init level.

#ifdef GODOT

#include "register_types.h"
#include "jdbasic_vm.h"
#include "jdb_script.h"
#include "jdb_script_language.h"
#include "jdb_script_resource.h"

#include <gdextension_interface.h>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/core/defs.hpp>
#include <godot_cpp/godot.hpp>

using namespace godot;

// Held alive between init / finish so the language pointer registered
// with the engine stays valid for the lifetime of the GDExtension.
static JdbScriptLanguage* s_language_instance = nullptr;

void initialize_jdb_godot_module(ModuleInitializationLevel p_level) {
    if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) return;

    // Tier 1 + Tier 2 (kept stable across the spinoff branch).
    ClassDB::register_class<JDBasicVM>();
    ClassDB::register_class<JDBScript>();

    // Tier 3 - ScriptLanguageExtension + ScriptExtension subclasses, plus
    // singleton registration so Godot's editor enumerates jdBasic as a
    // language alongside GDScript / C#.
    ClassDB::register_class<JdbScriptResource>();
    ClassDB::register_class<JdbScriptLanguage>();

    s_language_instance = memnew(JdbScriptLanguage);
    Engine::get_singleton()->register_script_language(s_language_instance);
}

void uninitialize_jdb_godot_module(ModuleInitializationLevel p_level) {
    if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) return;
    if (s_language_instance) {
        Engine::get_singleton()->unregister_script_language(s_language_instance);
        memdelete(s_language_instance);
        s_language_instance = nullptr;
    }
}

extern "C" {

GDExtensionBool GDE_EXPORT jdb_godot_library_init(
        GDExtensionInterfaceGetProcAddress p_get_proc_address,
        const GDExtensionClassLibraryPtr p_library,
        GDExtensionInitialization* r_initialization) {
    GDExtensionBinding::InitObject init_obj(p_get_proc_address, p_library, r_initialization);
    init_obj.register_initializer(initialize_jdb_godot_module);
    init_obj.register_terminator(uninitialize_jdb_godot_module);
    init_obj.set_minimum_library_initialization_level(MODULE_INITIALIZATION_LEVEL_SCENE);
    return init_obj.init();
}

}  // extern "C"

#endif  // GODOT

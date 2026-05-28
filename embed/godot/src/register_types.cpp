// GDExtension entry point. Godot calls jdb_godot_library_init when it
// loads addons/jdb_godot/bin/jdb_godot*.dll; we register JDBasicVM with
// the engine's ClassDB at SCENE init level.

#ifdef GODOT

#include "register_types.h"
#include "jdbasic_vm.h"

#include <gdextension_interface.h>
#include <godot_cpp/core/defs.hpp>
#include <godot_cpp/godot.hpp>

using namespace godot;

void initialize_jdb_godot_module(ModuleInitializationLevel p_level) {
    if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) return;
    ClassDB::register_class<JDBasicVM>();
}

void uninitialize_jdb_godot_module(ModuleInitializationLevel p_level) {
    if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) return;
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

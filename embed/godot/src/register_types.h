// GDExtension entry-point + class registration glue.
//
// Implements the two symbols godot-cpp expects:
//   jdb_godot_library_init     - called by Godot when loading the .dll
//   initialize_jdb_godot_module - per-init-level class registration

#pragma once

#ifdef GODOT

#include <godot_cpp/core/class_db.hpp>

using namespace godot;

void initialize_jdb_godot_module(ModuleInitializationLevel p_level);
void uninitialize_jdb_godot_module(ModuleInitializationLevel p_level);

#endif  // GODOT

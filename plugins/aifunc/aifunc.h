#pragma once

#include "ModuleInterface.h" // Include the new interface definition

// Use the standard C calling convention and name mangling
// to ensure the function name is predictable and stable across compilers.
#ifdef _WIN32
    // On Windows, we use __declspec(dllexport) to mark functions for export.
#define DLLEXPORT extern "C" __declspec(dllexport)
#else
    // On Linux/macOS, we use __attribute__((visibility("default"))).
#define DLLEXPORT extern "C" __attribute__((visibility("default")))
#endif

// Forward-declare the main interpreter class. We use a forward declaration
// to avoid a circular dependency, as the DLL doesn't need the full
// definition of NeReLaBasic, only a pointer to it.
class NeReLaBasic;

using NativeDLLFunction = void(*)(NeReLaBasic&, const std::vector<BasicValue>&, BasicValue*);

/**
 * @brief The single, well-known entry point for the DLL.
 *
 * Your jdBasic interpreter will look for this specific function by name
 * in any DLL it loads via the IMPORT command. This function is responsible
 * for registering all the features of this module (like the Adam optimizer)
 * with the interpreter instance.
 *
 * @param vm A pointer to the running NeReLaBasic interpreter instance.
 * @param services A pointer to the struct containing callback functions from the main app.
 */
DLLEXPORT void jdBasic_register_module(NeReLaBasic* vm, ModuleServices* services);

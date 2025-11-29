#pragma once

#include "ModuleInterface.h" // Your existing interface file

// Use the standard C calling convention and name mangling
#ifdef _WIN32
    // On Windows, we use __declspec(dllexport)
#define DLLEXPORT extern "C" __declspec(dllexport)
#else
    // On Linux/macOS, we use __attribute__((visibility("default")))
#define DLLEXPORT extern "C" __attribute__((visibility("default")))
#endif

// Forward-declare the main interpreter class
class NeReLaBasic;

/**
 * @brief The single, well-known entry point for the DLL.
 *
 * @param vm A pointer to the running NeReLaBasic interpreter instance.
 * @param services A pointer to the struct containing callback functions from the main app.
 */
DLLEXPORT void jdBasic_register_module(NeReLaBasic* vm, ModuleServices* services);

// whisper_integration.h
#pragma once

#include "ModuleInterface.h" 

#ifdef _WIN32
#define DLLEXPORT extern "C" __declspec(dllexport)
#else
#define DLLEXPORT extern "C" __attribute__((visibility("default")))
#endif

class NeReLaBasic;
// This using declaration simplifies the function pointer type definition.
using NativeDLLFunction = void(*)(NeReLaBasic&, const std::vector<BasicValue>&, BasicValue*);

/**
 * @brief The single, required entry point for the jdBasic DLL.
 * * When jdBasic's IMPORTDLL command is used, it looks for this specific function
 * within the loaded library. This function is responsible for registering all the
 * new commands and functions that the DLL provides.
 * * @param vm A pointer to the main NeReLaBasic virtual machine instance.
 * @param services A pointer to a struct containing callback functions (like Error::set)
 * that allow the DLL to safely interact with the jdBasic runtime.
 */
DLLEXPORT void jdBasic_register_module(NeReLaBasic* vm, ModuleServices* services);

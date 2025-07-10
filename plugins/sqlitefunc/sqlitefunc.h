// sqlitefunc.h
#pragma once

#include "ModuleInterface.h" 

#ifdef _WIN32
#define DLLEXPORT extern "C" __declspec(dllexport)
#else
#define DLLEXPORT extern "C" __attribute__((visibility("default")))
#endif

class NeReLaBasic;
using NativeDLLFunction = void(*)(NeReLaBasic&, const std::vector<BasicValue>&, BasicValue*);

// The single, well-known entry point for the DLL.
DLLEXPORT void jdBasic_register_module(NeReLaBasic* vm, ModuleServices* services);
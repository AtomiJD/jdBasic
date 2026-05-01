#pragma once
#include <string>

// Version info — updated by release builds
#define JDBASIC_VERSION    "1.0"
#define JDBASIC_BUILD_NUM  "2"
#define JDBASIC_BUILD_DATE "2026/04/15"

// Feature flags (set by compiler directives)
inline std::string jdbasic_features() {
    std::string f;
#ifdef COM
    if (!f.empty()) f += ", ";
    f += "COM";
#endif
#ifdef HTTP
    if (!f.empty()) f += ", ";
    f += "HTTP";
#endif
#ifdef USE_SERIAL
    if (!f.empty()) f += ", ";
    f += "Serial";
#endif
#ifdef GFX
    if (!f.empty()) f += ", ";
    f += "GFX";
#endif
#ifdef IMGUI
    if (!f.empty()) f += ", ";
    f += "ImGui";
#endif
#ifdef ONNX
    if (!f.empty()) f += ", ";
    f += "ONNX";
#endif
#ifdef LLM
    if (!f.empty()) f += ", ";
    f += "LLM";
#endif
#ifdef MCPSERVER
    if (!f.empty()) f += ", ";
    f += "MCP";
#endif
    if (f.empty()) f = "Core";
    return f;
}

inline std::string jdbasic_os() {
#if defined(_WIN32)
  #if defined(_WIN64)
    return "win32 64-bit";
  #else
    return "win32 32-bit";
  #endif
#elif defined(__linux__)
    return "linux";
#elif defined(__APPLE__)
    return "macOS";
#else
    return "unknown";
#endif
}

inline std::string jdbasic_banner() {
    std::string b;
    b += "jdBasic v" JDBASIC_VERSION " (Build " JDBASIC_BUILD_NUM ", " JDBASIC_BUILD_DATE "), OS: " + jdbasic_os() + "\n";
    b += "Features: " + jdbasic_features() + "\n";
    b += "Copyright (c) 2025-2026 Computerwelt AI Solutions LLC.\n";
    b += "All Rights Reserved.\n";
    b += "Type HELP for more infos.\n";
    b += "[F1-F4] Workspace  [F5] Run  [F7] History  [F8] Search  EXIT to Quit\n";
    return b;
}

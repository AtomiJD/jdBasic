#pragma once
#include <string>

// Version info — updated by release builds.
// `/DJDBASIC_BUILD_NUM=` and `/DJDBASIC_BUILD_DATE=` are injected by
// build.bat under the RELEASE flag; the fallbacks below kick in only
// for non-RELEASE builds (dev builds, --lint, IDE indexers). Guarded
// with #ifndef so the command-line define isn't redefined here — that
// was the source of the C4005 warning seen in every TU that included
// version.h.
#ifndef JDBASIC_VERSION
#define JDBASIC_VERSION    "1.0"
#endif
#ifndef JDBASIC_BUILD_NUM
#define JDBASIC_BUILD_NUM  "0"
#endif
#ifndef JDBASIC_BUILD_DATE
#define JDBASIC_BUILD_DATE "dev"
#endif

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
#ifdef OPENGL
    if (!f.empty()) f += ", ";
    f += "GL3.3";
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
#ifdef SQLITE
    if (!f.empty()) f += ", ";
    f += "SQLite";
#endif
#ifdef PYTHON
    if (!f.empty()) f += ", ";
    f += "Python";
#endif
    if (f.empty()) f = "Core";
    return f;
}

inline std::string jdbasic_os() {
#if defined(__EMSCRIPTEN__)
    return "Web";
#elif defined(_WIN32)
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
    b += "[F1-F4 / Ctrl+F1-F4] Workspace  [F5] Run  [F7] History  [F8] Search  EXIT to Quit\n";
    return b;
}

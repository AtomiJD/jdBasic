#pragma once

// ============================================================================
//                  PROJECT-WIDE PREPROCESSOR DEFINITIONS
// ============================================================================
// This file centralizes all major feature flags and configuration macros
// that were previously defined in the Visual Studio project file (.vcxproj).
// ----------------------------------------------------------------------------

// --- Major Feature Flags ---

// Target is the Raspberry Pi RP2350 microcontroller.
// #define JD_OS_EMBEDDED
// #define RP2350

// If you are on a linux server uncomment the following line
// #define SERVER

// Enables all SDL3-related code for graphics, sound, sprites, etc.
#if defined(_WIN32) || defined(__linux__) || defined(__APPLE__) || defined(__EMSCRIPTEN__)
#if !defined(SERVER)
#define SDL3
#endif
#endif

// Enables the cpp-httplib networking features for HTTP client and server.
#if defined(_WIN32) || defined(__linux__) || defined(__APPLE__)
#define HTTP
#endif

// --- Windows-Specific Compatibility Macros ---

// Enables COM/ActiveX support for interacting with Windows objects (e.g., Excel).
// This should only be defined for Windows builds.
#if defined(_WIN32) 
#define JDCOM
#endif

// Prevents the <windows.h> header from defining min() and max() as macros,
// which would conflict with std::min and std::max from <algorithm>.
#if defined(_WIN32) 
#define NOMINMAX
#endif

// Prevents <windows.h> from automatically including the older <winsock.h>.
// This is often used to avoid conflicts when you intend to use <winsock2.h>
// or another networking library.
#if defined(_WIN32) 
#define _WINSOCKAPI_
#endif

// --- Library-Specific Configuration ---

// Tells the cpp-httplib library to include support for HTTPS by linking
// against OpenSSL.
#if defined(_WIN32) || defined(__linux__) || defined(__APPLE__)
#define CPPHTTPLIB_OPENSSL_SUPPORT
#endif

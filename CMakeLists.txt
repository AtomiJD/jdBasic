#================================================================================
# CMakeLists.txt for jdBasic (Cross-platform for Linux and macOS)
#
# This file was generated based on a Visual Studio solution.
# It is configured for building on a Linux or macOS system with GCC/Clang.
# Place this file in the root directory of your project.
#================================================================================

# 1. Set the minimum required version of CMake and define the project.
# C++20 is used, so a modern CMake version is recommended.
cmake_minimum_required(VERSION 3.20)
project(JDBasicProject VERSION 1.0 LANGUAGES CXX)

#================================================================================
# Global Settings & Options
#================================================================================

# Set the C++ standard to C++20, as specified in the vcxproj.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Build option for the server version.
option(BUILD_SERVER "Build the server-only version without GUI (SDL3)" OFF)

# Add a message to display the build type.
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
  message(STATUS "Build type not specified, defaulting to 'Release'")
endif()
message(STATUS "Build type is: ${CMAKE_BUILD_TYPE}")
if(BUILD_SERVER)
    message(STATUS "Server build enabled: SDL3 components will be excluded.")
else()
    message(STATUS "Standard build enabled: Including SDL3 components.")
endif()

#================================================================================
# Find Package Dependencies
#================================================================================

find_package(PkgConfig REQUIRED)

# macOS: Add hints for finding packages installed with Homebrew.
if(APPLE)
  # This makes CMake search Homebrew's default installation paths.
  if(EXISTS "/opt/homebrew/bin/brew")
    set(HOMEBREW_PREFIX "/opt/homebrew") # Apple Silicon
  else()
    set(HOMEBREW_PREFIX "/usr/local") # Intel
  endif()
  list(APPEND CMAKE_PREFIX_PATH "${HOMEBREW_PREFIX}")

  # Provide a specific hint for OpenSSL, which is often keg-only.
  find_package(OpenSSL REQUIRED HINTS "${HOMEBREW_PREFIX}/opt/openssl@3")
else()
  # Find OpenSSL on Linux
  find_package(OpenSSL)
endif()

# Find SDL3 and its extension libraries
if(NOT BUILD_SERVER)
    find_package(SDL3 REQUIRED)
    find_package(SDL3_image REQUIRED)
    find_package(SDL3_ttf REQUIRED)
endif()

# Find ncurses (for console I/O, replacing conio.h)
find_package(Curses REQUIRED)

# macOS: Find the readline library using pkg-config for better compatibility.
pkg_check_modules(Readline REQUIRED readline)

# Find the {fmt} library
find_package(fmt REQUIRED)

# Find the Threads library (pthreads on Linux/macOS) for std::thread support
find_package(Threads REQUIRED)

#================================================================================
# Define the main executable: jdBasic
#================================================================================

# List all the source files for the jdBasic executable.
set(JDBASIC_SOURCES
    source/AIFunctions.cpp
    source/ArrayFunctions.cpp
    source/BuiltinFunctions.cpp
    source/BetterCodeFunctions.cpp
    source/Commands.cpp
    source/Compiler.cpp
    source/DAPHandler.cpp
    source/Error.cpp
    source/ExpressionParser.cpp
    source/Graphics.cpp
    source/LocaleManager.cpp
    source/NeReLaBasic.cpp
    source/NeReLaBasicInterpreter.cpp
    source/NetworkManager.cpp
    source/SDLFunctions.cpp
    source/SoundSystem.cpp
    source/SpriteSystem.cpp
    source/Statements.cpp
    source/StringUtils.cpp
    source/TextEditor.cpp
    source/TextIO.cpp
    source/TileMapSystem.cpp
)

# Create the executable target.
add_executable(jdBasic ${JDBASIC_SOURCES})

#================================================================================
# Configure Target: jdBasic
#================================================================================

# --- Include Directories ---
# Add the necessary include paths for jdBasic.
target_include_directories(jdBasic PRIVATE
    # Project-specific include directory
    "${CMAKE_CURRENT_SOURCE_DIR}/include"

    # Header-only libraries included in the project source tree
    "${CMAKE_CURRENT_SOURCE_DIR}/libs/cpp-httplib"

    # Include directories from found packages
    ${OPENSSL_INCLUDE_DIR}
    ${Readline_INCLUDE_DIRS}
    ${FMT_INCLUDE_DIRS}
    ${CURSES_INCLUDE_DIR}
)

# Add SDL include directories only if not a server build.
if(NOT BUILD_SERVER)
    target_include_directories(jdBasic PRIVATE
        ${SDL3_INCLUDE_DIRS}
        ${SDL3_IMAGE_INCLUDE_DIRS}
        ${SDL3_TTF_INCLUDE_DIRS}
    )
endif()

# --- Compile Definitions ---
# These are equivalent to the PreprocessorDefinitions in the vcxproj.
target_compile_definitions(jdBasic PRIVATE
    # General flags
    SDL3
    HTTP
    CPPHTTPLIB_OPENSSL_SUPPORT
    JDEDIT
    # Platform and build-type specific flags
    $<$<NOT:$<BOOL:${BUILD_SERVER}>>:SDL3> # Define SDL3 only for non-server builds
    $<$<BOOL:${BUILD_SERVER}>:SERVER>     # Define SERVER for server builds
    # Flags for non-Windows builds
    $<$<NOT:$<CONFIG:Debug>>:NDEBUG> # Add NDEBUG for non-debug builds
    $<$<CONFIG:Debug>:_DEBUG>      # Add _DEBUG for debug builds
)

# --- Compiler Flags ---
# Add platform-specific compiler flags.
# Enable OpenMP only for GNU GCC or standard LLVM Clang, but NOT AppleClang.
if (CMAKE_CXX_COMPILER_ID MATCHES "GNU" OR (CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND NOT APPLE))
    # Enable OpenMP for Release builds, as in the vcxproj
    target_compile_options(jdBasic PRIVATE
        $<$<CONFIG:Release>:-fopenmp>
    )
endif()

# --- Link Libraries ---
# Link jdBasic against all its dependencies.
target_link_libraries(jdBasic PRIVATE
    # Libraries found by find_package
    OpenSSL::SSL
    OpenSSL::Crypto
    ${CURSES_LIBRARIES}
    ${Readline_LIBRARIES} # Use the variable from pkg_check_modules
    fmt::fmt
    Threads::Threads
)

# Add SDL libraries only if not a server build.
if(NOT BUILD_SERVER)
    target_link_libraries(jdBasic PRIVATE
        SDL3::SDL3
        SDL3_image::SDL3_image
        SDL3_ttf::SDL3_ttf
    )
endif()

#================================================================================
# Installation
#
# This section defines what happens when 'make install' is run.
#================================================================================
install(TARGETS jdBasic
    RUNTIME DESTINATION bin
)

message(STATUS "Installation configured. Run 'make install' after building.")
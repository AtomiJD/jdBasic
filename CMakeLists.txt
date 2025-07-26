#================================================================================
# CMakeLists.txt for jdBasic and related projects
#
# This file was generated based on a Visual Studio solution.
# It is configured for building on a Linux system with GCC/Clang.
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

# Add a message to display the build type.
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
  message(STATUS "Build type not specified, defaulting to 'Release'")
endif()
message(STATUS "Build type is: ${CMAKE_BUILD_TYPE}")

#================================================================================
# Find Package Dependencies
#
# This section finds the libraries your project depends on.
# On Linux, you'll need to install the development packages for these libraries.
# For example, on Debian/Ubuntu:
# sudo apt-get install libsdl3-dev libsdl3-image-dev libsdl3-ttf-dev libssl-dev
#================================================================================

# Find OpenSSL
find_package(OpenSSL)

# Find SDL3 and its extension libraries
# find_package(SDL3 REQUIRED)
# find_package(SDL3_image REQUIRED)
# find_package(SDL3_ttf REQUIRED)

# Find ncurses (for console I/O, replacing conio.h)
find_package(Curses REQUIRED)

# Find the {fmt} library to provide std::format functionality for older compilers
find_package(fmt REQUIRED)

# Find the Threads library (pthreads on Linux) for std::thread support
find_package(Threads REQUIRED)

# Note on TensorFlow:
# Finding TensorFlow can be complex. You need to have the TensorFlow C++ library
# installed on your system. You might need to provide the path to your
# TensorFlow installation by setting TENSORFLOW_DIR in your CMake configuration.
# For now, we'll define a placeholder.
# find_package(TensorFlow) # Uncomment when you have TensorFlow set up.

#================================================================================
# Define the main executable: jdBasic
#================================================================================

# List all the source files for the jdBasic executable.
set(JDBASIC_SOURCES
    source/AIFunctions.cpp
    source/BuiltinFunctions.cpp
    source/Commands.cpp
    source/Compiler.cpp
    source/DAPHandler.cpp
    source/Error.cpp
    source/Graphics.cpp
    source/LocaleManager.cpp
    source/NeReLaBasic.cpp
    source/NeReLaBasicInterpreter.cpp
    source/NetworkManager.cpp
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
    # ${OPENSSL_INCLUDE_DIR}
    # ${SDL3_INCLUDE_DIRS}
    # ${SDL3_IMAGE_INCLUDE_DIRS}
    # ${SDL3_TTF_INCLUDE_DIRS}

    ${FMT_INCLUDE_DIRS}

    ${CURSES_INCLUDE_DIR}

    # TensorFlow include path (adjust if needed)
    # "${CMAKE_CURRENT_SOURCE_DIR}/libs/tensorflow/include"
)

# --- Compile Definitions ---
# These are equivalent to the PreprocessorDefinitions in the vcxproj.
target_compile_definitions(jdBasic PRIVATE
    # General flags
    # SDL3
    # HTTP
    # JDCOM
    # CPPHTTPLIB_OPENSSL_SUPPORT

    # Flags for Linux (replacing Windows-specific ones like _WINSOCKAPI_)
    $<$<NOT:$<CONFIG:Debug>>:NDEBUG> # Add NDEBUG for non-debug builds
    $<$<CONFIG:Debug>:_DEBUG>      # Add _DEBUG for debug builds
)

# --- Compiler Flags ---
# Add platform-specific compiler flags.
if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    # Enable OpenMP for Release builds, as in the vcxproj
    target_compile_options(jdBasic PRIVATE
        $<$<CONFIG:Release>:-fopenmp>
    )
endif()

# --- Link Libraries ---
# Link jdBasic against all its dependencies.
target_link_libraries(jdBasic PRIVATE
    # Libraries found by find_package
    # OpenSSL::SSL
    # OpenSSL::Crypto
    # SDL3::SDL3
    # SDL3_image::SDL3_image
    # SDL3_ttf::SDL3_ttf

    # TensorFlow library (adjust if needed)
    # This assumes you have a 'tensorflow' target or library file.
    # You might need to specify the full path to the .so file.
    # "${CMAKE_CURRENT_SOURCE_DIR}/libs/tensorflow/lib/libtensorflow.so"

    ${CURSES_LIBRARIES}
    fmt::fmt
    Threads::Threads

    # Add the dynamic linking library (libdl)
    dl
)

#================================================================================
# Plugin & Tester Projects (Placeholders)
#
# Your solution contains other projects. The best way to handle this in CMake
# is to give each one its own CMakeLists.txt and add it here using
# add_subdirectory().
#
# Example structure:
# root/
# |- CMakeLists.txt (this file)
# |- jdBasic/
# |  |- (source files)
# |- plugins/
# |  |- aifunc/
# |  |  |- CMakeLists.txt
# |  |  |- (source files)
#
# You would then uncomment the lines below.
#================================================================================

# message(STATUS "Adding subdirectories for plugins and testers...")
# add_subdirectory(plugins/aifunc)
# add_subdirectory(plugins/sqlitefunc)
# add_subdirectory(plugins/tffunc)
# add_subdirectory(jdTester) # Assuming jdTester is moved into the project root


#================================================================================
# Installation (Optional)
#
# This section defines how to install the project.
#================================================================================
# install(TARGETS jdBasic
#     RUNTIME DESTINATION bin
#     LIBRARY DESTINATION lib
#     ARCHIVE DESTINATION lib/static
# )

message(STATUS "Configuration of jdBasic is complete.")


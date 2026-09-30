cmake_minimum_required(VERSION 3.16)
project(basic_interpreter VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Optimized release build by default
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

if(MSVC)
    set(CMAKE_CXX_FLAGS_RELEASE "/O2 /DNDEBUG")
else()
    set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")
endif()

add_executable(basic
    src/main.cpp
    src/lexer.cpp
    src/parser.cpp
    src/compiler.cpp
    src/vm.cpp
    src/vm_builtins_math.cpp
    src/vm_builtins_string.cpp
    src/vm_builtins_data.cpp
    src/vm_builtins_array.cpp
    src/vm_builtins_datetime.cpp
    src/vm_builtins_codec.cpp
    src/vm_builtins_console.cpp
    src/vm_builtins_file.cpp
    src/vm_builtins_system.cpp
    src/vm_builtins_async.cpp
)

target_include_directories(basic PRIVATE src)

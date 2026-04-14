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
)

target_include_directories(basic PRIVATE src)

@echo off
setlocal enabledelayedexpansion
REM Build the jdBasic Runtime DLL (jdbrt.dll)
REM This DLL exposes all VM builtins via a C-API for native-compiled executables.

set MSVC=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.44.35207
set SDK=C:\Program Files (x86)\Windows Kits\10
set SDKV=10.0.26100.0
set "CC=%MSVC%\bin\Hostx64\x64\cl.exe"

if not exist build mkdir build

set DEFS=/DNDEBUG /DJDRT_EXPORTS
set EXTRA_SRC=
set EXTRA_INC=
set EXTRA_LIB=user32.lib ws2_32.lib iphlpapi.lib
set EXTRA_LIBPATH=

REM Parse arguments for optional modules
for %%A in (%*) do (
    if /I "%%A"=="COM" (
        set DEFS=!DEFS! /DCOM
        set EXTRA_SRC=!EXTRA_SRC! src\com.cpp
        echo [+] COM
    )
    if /I "%%A"=="HTTP" (
        set DEFS=!DEFS! /DHTTP
        set EXTRA_SRC=!EXTRA_SRC! src\http.cpp
        set EXTRA_INC=!EXTRA_INC! /I"C:\Program Files\OpenSSL-Win64\include"
        set EXTRA_LIB=!EXTRA_LIB! "C:\Program Files\OpenSSL-Win64\lib\VC\x64\MD\libssl.lib" "C:\Program Files\OpenSSL-Win64\lib\VC\x64\MD\libcrypto.lib"
        echo [+] HTTP
    )
    if /I "%%A"=="GFX" (
        set DEFS=!DEFS! /DGFX
        set EXTRA_SRC=!EXTRA_SRC! src\graphics.cpp src\sprites.cpp src\tiledmap.cpp
        set EXTRA_INC=!EXTRA_INC! /Ilibs\SDL3-3.4.8\include /Ilibs\SDL3_ttf-3.2.2\include /Ilibs\SDL3_image-3.4.4\include /Ilibs\SDL3_mixer-3.2.2\include /Ilibs\SDL3_mixer-3.2.2\include\SDL3_mixer
        set EXTRA_LIBPATH=!EXTRA_LIBPATH! /LIBPATH:libs\SDL3-3.4.8\lib\x64 /LIBPATH:libs\SDL3_ttf-3.2.2\lib\x64 /LIBPATH:libs\SDL3_image-3.4.4\lib\x64 /LIBPATH:libs\SDL3_mixer-3.2.2\lib\x64
        set EXTRA_LIB=!EXTRA_LIB! SDL3.lib SDL3_ttf.lib SDL3_image.lib SDL3_mixer.lib
        echo [+] GFX
    )
    if /I "%%A"=="IMGUI" (
        set DEFS=!DEFS! /DIMGUI
        set EXTRA_SRC=!EXTRA_SRC! libs\imgui\imgui.cpp libs\imgui\imgui_draw.cpp libs\imgui\imgui_tables.cpp libs\imgui\imgui_widgets.cpp libs\imgui\backends\imgui_impl_sdl3.cpp libs\imgui\backends\imgui_impl_sdlrenderer3.cpp
        set EXTRA_INC=!EXTRA_INC! /Ilibs\imgui /Ilibs\imgui\backends
        echo [+] ImGui
    )
    if /I "%%A"=="OPENGL" (
        set DEFS=!DEFS! /DOPENGL
        set EXTRA_SRC=!EXTRA_SRC! src\opengl.cpp
        set EXTRA_LIB=!EXTRA_LIB! opengl32.lib
        echo [+] OpenGL 3.3 Core
    )
    if /I "%%A"=="SERIAL" (
        set DEFS=!DEFS! /DUSE_SERIAL
        set EXTRA_SRC=!EXTRA_SRC! src\serial.cpp
        echo [+] Serial
    )
    if /I "%%A"=="FTXUI" (
        REM /D UNICODE/_UNICODE matches the ftxui.lib ABI (same rule as build.bat).
        set DEFS=!DEFS! /DFTXUI /DUNICODE /D_UNICODE
        set EXTRA_INC=!EXTRA_INC! /Ilibs\ftxui\include
        if not exist libs\ftxui\build\ftxui.lib (
            echo [+] FTXUI lib missing - building one-time...
            call build_ftxui.bat
        )
        set EXTRA_LIB=!EXTRA_LIB! libs\ftxui\build\ftxui.lib
        set HAVE_FTXUI=1
        echo [+] FTXUI
    )
    if /I "%%A"=="TUI" (
        set DEFS=!DEFS! /DTUI /DUNICODE /D_UNICODE
        set EXTRA_SRC=!EXTRA_SRC! src\tui.cpp src\tui_state.cpp
        set WANT_TUI=1
        echo [+] TUI
    )
)

REM TUI implies FTXUI — pull the lib in if only TUI was passed.
if defined WANT_TUI if not defined HAVE_FTXUI (
    set DEFS=!DEFS! /DFTXUI /DUNICODE /D_UNICODE
    set EXTRA_INC=!EXTRA_INC! /Ilibs\ftxui\include
    if not exist libs\ftxui\build\ftxui.lib (
        echo [+] FTXUI lib missing - building one-time...
        call build_ftxui.bat
    )
    set EXTRA_LIB=!EXTRA_LIB! libs\ftxui\build\ftxui.lib
    set HAVE_FTXUI=1
    echo [+] TUI implies FTXUI - lib auto-enabled
)

REM ftxui.lib is /MD; mixing /MT vm_bridge with /MD ftxui hits LNK2038.
REM Match the CRT whenever FTXUI is in the mix.
set CRT_FLAG=/MT
if defined HAVE_FTXUI set CRT_FLAG=/MD

echo Building jdbrt.dll ...

REM /MP32: full parallel compile (32 threads, ample RAM); see build.bat.
"%CC%" /std:c++17 /O2 /fp:fast /EHa /MP32 %CRT_FLAG% %DEFS% /LD ^
  /I"%MSVC%\include" ^
  /I"%SDK%\Include\%SDKV%\ucrt" ^
  /I"%SDK%\Include\%SDKV%\um" ^
  /I"%SDK%\Include\%SDKV%\shared" ^
  /Isrc !EXTRA_INC! ^
  src\vm_bridge.cpp src\vm.cpp src\lexer.cpp src\parser.cpp src\compiler.cpp src\console.cpp src\editor.cpp src\dap.cpp src\ffi.cpp src\sound.cpp src\gui.cpp src\ai.cpp src\llm.cpp src\channels.cpp src\file_streams.cpp src\jdb_embed_api.cpp !EXTRA_SRC! ^
  /Fe:build\jdbrt.dll ^
  /Fo:build\ ^
  /link ^
  /LIBPATH:"%MSVC%\lib\x64" ^
  /LIBPATH:"%SDK%\Lib\%SDKV%\ucrt\x64" ^
  /LIBPATH:"%SDK%\Lib\%SDKV%\um\x64" ^
  !EXTRA_LIBPATH! ^
  !EXTRA_LIB!

if %ERRORLEVEL%==0 (
    echo.
    echo BUILD OK: build\jdbrt.dll
    echo   Exports: jdrt_*       native-EXE  -^> runtime bridge
    echo            jdb_embed_*  host app    -^> embedded VM
) else (
    echo.
    echo BUILD FAILED
)

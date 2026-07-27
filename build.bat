@echo off
setlocal enabledelayedexpansion

set MSVC=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.44.35207
set SDK=C:\Program Files (x86)\Windows Kits\10
set SDKV=10.0.26100.0
set OPENSSL=C:\Program Files\OpenSSL-Win64
set "CC=%MSVC%\bin\Hostx64\x64\cl.exe"

if not exist build mkdir build

set DEFS=/DNDEBUG
set EXTRA_SRC=
set EXTRA_INC=
set EXTRA_LIB=user32.lib ws2_32.lib iphlpapi.lib gdi32.lib windowscodecs.lib ole32.lib
set EXTRA_LIBPATH=
set COPY_DLLS=

REM Build counter file
set BUILD_NUM_FILE=build_number.txt
if not exist %BUILD_NUM_FILE% echo 0> %BUILD_NUM_FILE%

REM Parse arguments
for %%A in (%*) do (
    if /I "%%A"=="COM" (
        set DEFS=!DEFS! /DCOM
        set EXTRA_SRC=!EXTRA_SRC! src\com.cpp
        echo [+] COM Automation
    )
    if /I "%%A"=="HTTP" (
        set DEFS=!DEFS! /DHTTP
        set EXTRA_SRC=!EXTRA_SRC! src\http.cpp
        set EXTRA_INC=!EXTRA_INC! /I"%OPENSSL%\include"
        set EXTRA_LIB=!EXTRA_LIB! "%OPENSSL%\lib\VC\x64\MD\libssl.lib" "%OPENSSL%\lib\VC\x64\MD\libcrypto.lib"
        set COPY_OPENSSL=1
        echo [+] HTTP/HTTPS - OpenSSL
    )
    if /I "%%A"=="SERIAL" (
        set DEFS=!DEFS! /DUSE_SERIAL
        set EXTRA_SRC=!EXTRA_SRC! src\serial.cpp
        echo [+] Serial Communication
    )
    if /I "%%A"=="GFX" (
        set DEFS=!DEFS! /DGFX
        set EXTRA_SRC=!EXTRA_SRC! src\graphics.cpp src\sprites.cpp src\tiledmap.cpp
        set EXTRA_INC=!EXTRA_INC! /Ilibs\SDL3-3.4.8\include /Ilibs\SDL3_ttf-3.2.2\include /Ilibs\SDL3_image-3.4.4\include /Ilibs\SDL3_mixer-3.2.2\include /Ilibs\SDL3_mixer-3.2.2\include\SDL3_mixer
        set EXTRA_LIBPATH=!EXTRA_LIBPATH! /LIBPATH:libs\SDL3-3.4.8\lib\x64 /LIBPATH:libs\SDL3_ttf-3.2.2\lib\x64 /LIBPATH:libs\SDL3_image-3.4.4\lib\x64 /LIBPATH:libs\SDL3_mixer-3.2.2\lib\x64
        set EXTRA_LIB=!EXTRA_LIB! SDL3.lib SDL3_ttf.lib SDL3_image.lib SDL3_mixer.lib
        set COPY_DLLS=1
        echo [+] Graphics/Audio - SDL3 + TTF + Image + Mixer
    )
    if /I "%%A"=="SOUND" (
        set DEFS=!DEFS! /DSOUND_DSP
        echo [+] SOUND - sequencer DSP, pull mode, no device
    )
    if /I "%%A"=="FX" (
        set DEFS=!DEFS! /DFX
        echo [+] FX - WAV I/O + effects chain, self-contained no external libs
    )
    if /I "%%A"=="MIDI" (
        set DEFS=!DEFS! /DMIDI /D__WINDOWS_MM__
        set EXTRA_SRC=!EXTRA_SRC! libs\rtmidi\RtMidi.cpp
        set EXTRA_INC=!EXTRA_INC! /Ilibs\rtmidi
        set EXTRA_LIB=!EXTRA_LIB! winmm.lib
        echo [+] MIDI - RtMidi WinMM
    )
    if /I "%%A"=="MINIAUDIO" (
        set DEFS=!DEFS! /DMINIAUDIO
        set EXTRA_INC=!EXTRA_INC! /Ilibs\miniaudio
        echo [+] MINIAUDIO - realtime audio device engine
    )
    if /I "%%A"=="FORMS" (
        set DEFS=!DEFS! /DFORMS
        set EXTRA_SRC=!EXTRA_SRC! src\forms_win32.cpp
        set EXTRA_LIB=!EXTRA_LIB! comctl32.lib
        echo [+] Forms - native Win32 windows and controls
    )
    if /I "%%A"=="IMGUI" (
        set DEFS=!DEFS! /DIMGUI
        set EXTRA_SRC=!EXTRA_SRC! libs\imgui\imgui.cpp libs\imgui\imgui_draw.cpp libs\imgui\imgui_tables.cpp libs\imgui\imgui_widgets.cpp libs\imgui\backends\imgui_impl_sdl3.cpp libs\imgui\backends\imgui_impl_sdlrenderer3.cpp
        set EXTRA_INC=!EXTRA_INC! /Ilibs\imgui /Ilibs\imgui\backends
        echo [+] ImGui
    )
    if /I "%%A"=="LLM" (
        set DEFS=!DEFS! /DLLM
        set EXTRA_INC=!EXTRA_INC! /Ilibs\llama
        set EXTRA_LIBPATH=!EXTRA_LIBPATH! /LIBPATH:libs\llama
        set EXTRA_LIB=!EXTRA_LIB! llama.lib ggml.lib ggml-base.lib
        echo [+] LLM - llama.cpp
    )
    if /I "%%A"=="ONNX" (
        set DEFS=!DEFS! /DONNX
        set EXTRA_INC=!EXTRA_INC! /Ilibs\onnxruntime\include
        set EXTRA_LIBPATH=!EXTRA_LIBPATH! /LIBPATH:libs\onnxruntime\lib
        set EXTRA_LIB=!EXTRA_LIB! onnxruntime.lib
        echo [+] ONNX Runtime - AI Inference
    )
    if /I "%%A"=="NATIVEC" (
        set DEFS=!DEFS! /DLLVM_CODEGEN
        set EXTRA_SRC=!EXTRA_SRC! src\llvm_codegen.cpp
        set EXTRA_INC=!EXTRA_INC! /Ilibs\LLVM\include
        set EXTRA_LIBPATH=!EXTRA_LIBPATH! /LIBPATH:libs\LLVM\lib
        set EXTRA_LIB=!EXTRA_LIB! LLVM-C.lib
        set COPY_LLVM=1
        echo [+] LLVM Native Compiler
    )
    if /I "%%A"=="MCPSERVER" (
        set DEFS=!DEFS! /DMCPSERVER
        set EXTRA_SRC=!EXTRA_SRC! src\mcp_stdio.cpp
        echo [+] MCP Server - Model Context Protocol stdio
    )
    if /I "%%A"=="SQLITE" (
        set DEFS=!DEFS! /DSQLITE
        set EXTRA_SRC=!EXTRA_SRC! src\sql.cpp
        set EXTRA_INC=!EXTRA_INC! /Ibridges\sqlitebridge
        set WANT_SQLITE=1
        echo [+] SQLite - embedded database engine, statically linked
    )
    if /I "%%A"=="PYTHON" (
        set DEFS=!DEFS! /DPYTHON
        set WANT_PYTHON=1
        echo [+] Python - embedded CPython interpreter [PYTHON$ / PY.*]
    )
    if /I "%%A"=="OPENGL" (
        REM Requires GFX - shares SDL3 init and event loop. We don't auto-imply
        REM GFX here, caller must pass GFX explicitly so they see the cost.
        set DEFS=!DEFS! /DOPENGL
        set EXTRA_SRC=!EXTRA_SRC! src\opengl.cpp
        set EXTRA_LIB=!EXTRA_LIB! opengl32.lib
        set WANT_OPENGL=1
        echo [+] OpenGL 3.3 Core - separate GL.WINDOW context
    )
    if /I "%%A"=="FTXUI" (
        REM /D UNICODE/_UNICODE matches the libs\ftxui\build\ftxui.lib ABI;
        REM mismatched runtime would surface as link errors on string types.
        set DEFS=!DEFS! /DFTXUI /DUNICODE /D_UNICODE
        set EXTRA_SRC=!EXTRA_SRC! src\repl_ftxui.cpp
        set EXTRA_INC=!EXTRA_INC! /Ilibs\ftxui\include
        if not exist libs\ftxui\build\ftxui.lib (
            echo [+] FTXUI lib missing - building one-time...
            call build_ftxui.bat
        )
        set EXTRA_LIB=!EXTRA_LIB! libs\ftxui\build\ftxui.lib
        set HAVE_FTXUI=1
        echo [+] FTXUI - Terminal UI
    )
    if /I "%%A"=="TUI" (
        REM TUI.* namespace - scripts target FTXUI through immediate-mode API.
        REM Implies FTXUI, drag the lib in if the user didn't pass it.
        set DEFS=!DEFS! /DTUI /DUNICODE /D_UNICODE
        set EXTRA_SRC=!EXTRA_SRC! src\tui.cpp src\tui_state.cpp
        set WANT_TUI=1
        echo [+] TUI - Script-facing terminal UI namespace
    )
    if /I "%%A"=="RELEASE" set WANT_RELEASE=1
)

REM Bump the build number for RELEASE builds. Done at top level, NOT inside the
REM for-loop body, because a < or > file redirect inside that parenthesized
REM block silently fails under cmd, so the old in-loop version never ran.
if not defined WANT_RELEASE goto :after_release
set /p BNUM=< %BUILD_NUM_FILE%
set /a BNUM=BNUM+1
> %BUILD_NUM_FILE% echo !BNUM!
set BDATE=%date:~6,4%/%date:~3,2%/%date:~0,2%-%time:~0,2%:%time:~3,2%.%time:~6,2%
set BDATE=!BDATE: =0!
set DEFS=!DEFS! /DJDBASIC_BUILD_NUM=\"!BNUM!\" /DJDBASIC_BUILD_DATE=\"!BDATE!\"
echo [+] RELEASE Build !BNUM! - !BDATE!
:after_release

REM OPENGL requires GFX (SDL_Window + SDL init come from there).
if defined WANT_OPENGL (
    echo %DEFS% | findstr /C:"/DGFX" >nul
    if errorlevel 1 (
        echo [!] OPENGL needs GFX - aborting. Pass GFX OPENGL together.
        exit /b 1
    )
)

REM SQLITE links the amalgamation; compile it once into build\sqlite3.obj
REM (plain C, slow to compile, never changes - same caching idea as ftxui).
if defined WANT_SQLITE (
    if not exist bridges\sqlitebridge\sqlite3.c (
        echo [!] SQLITE needs the amalgamation - download sqlite3.c/sqlite3.h
        echo     from https://sqlite.org/download.html into bridges\sqlitebridge\
        exit /b 1
    )
    if not exist build\sqlite3.obj (
        echo [+] Compiling SQLite amalgamation one-time...
        "%CC%" /nologo /c /O2 /MD /I"%MSVC%\include" /I"%SDK%\Include\%SDKV%\ucrt" /I"%SDK%\Include\%SDKV%\um" /I"%SDK%\Include\%SDKV%\shared" /Ibridges\sqlitebridge /Fo:build\sqlite3.obj bridges\sqlitebridge\sqlite3.c
    )
    set EXTRA_LIB=!EXTRA_LIB! build\sqlite3.obj
)

REM PYTHON embeds CPython. Resolve the interpreter home (JDB_PYTHON_HOME wins,
REM else the per-user pythoncore package) and wire its headers + import lib.
if defined WANT_PYTHON (
    if defined JDB_PYTHON_HOME (
        set "PYHOME=%JDB_PYTHON_HOME%"
    ) else (
        set "PYHOME=%LOCALAPPDATA%\python\pythoncore-3.14-64"
    )
    if not exist "!PYHOME!\include\Python.h" (
        echo [!] PYTHON needs CPython dev headers at !PYHOME!\include\Python.h
        echo     Set JDB_PYTHON_HOME to a Python install with include\ + libs\
        exit /b 1
    )
    set EXTRA_INC=!EXTRA_INC! /I"!PYHOME!\include"
    set EXTRA_LIBPATH=!EXTRA_LIBPATH! /LIBPATH:"!PYHOME!\libs"
    set EXTRA_LIB=!EXTRA_LIB! python314.lib
    echo [+] Python home: !PYHOME!
)

REM TUI implies FTXUI, pull the lib in if the user only passed TUI.
if defined WANT_TUI if not defined HAVE_FTXUI (
    set DEFS=!DEFS! /DFTXUI /DUNICODE /D_UNICODE
    set EXTRA_SRC=!EXTRA_SRC! src\repl_ftxui.cpp
    set EXTRA_INC=!EXTRA_INC! /Ilibs\ftxui\include
    if not exist libs\ftxui\build\ftxui.lib (
        echo [+] FTXUI lib missing - building one-time...
        call build_ftxui.bat
    )
    set EXTRA_LIB=!EXTRA_LIB! libs\ftxui\build\ftxui.lib
    echo [+] TUI implies FTXUI - lib auto-enabled
)

REM Compile resources (icon, manifest, version info)
set "RC=%SDK%\bin\%SDKV%\x64\rc.exe"
"%RC%" /fo build\version.res resources\version.rc >nul 2>&1

REM /MP32 → cl spawns up to 32 worker processes for the compile phase
REM (machine has 32 logical threads + 64 GB RAM, so memory headroom is fine).
REM /MP without a number uses min(NUMBER_OF_PROCESSORS, 8); explicit 32 wins.
REM /MD: link the DLL CRT. ftxui.lib (and OpenSSL's MD libs) expect this;
REM mismatched runtimes show up as unresolved __imp__* symbols.
"%CC%" /std:c++17 /O2 /EHa /MP32 /MD %DEFS% ^
  /I"%MSVC%\include" ^
  /I"%SDK%\Include\%SDKV%\ucrt" ^
  /I"%SDK%\Include\%SDKV%\um" ^
  /I"%SDK%\Include\%SDKV%\shared" ^
  /Isrc /Ilibs\eigen !EXTRA_INC! ^
  src\main.cpp src\lexer.cpp src\parser.cpp src\compiler.cpp src\vm.cpp src\console.cpp src\editor.cpp src\dap.cpp src\ffi.cpp src\sound.cpp src\audio_fx.cpp src\midi.cpp src\audio_io.cpp src\gui.cpp src\ai.cpp src\llm.cpp src\channels.cpp src\file_streams.cpp src\numerics.cpp src\screencap.cpp src\pybridge.cpp !EXTRA_SRC! ^
  /Fe:build\jdBasic.exe ^
  /Fo:build\ ^
  /link ^
  /LIBPATH:"%MSVC%\lib\x64" ^
  /LIBPATH:"%SDK%\Lib\%SDKV%\ucrt\x64" ^
  /LIBPATH:"%SDK%\Lib\%SDKV%\um\x64" ^
  !EXTRA_LIBPATH! ^
  !EXTRA_LIB! ^
  build\version.res

if %ERRORLEVEL%==0 (
    echo.
    echo BUILD OK: build\jdBasic.exe
    if exist help.txt copy /Y help.txt build\ >nul 2>&1
    if defined COPY_DLLS (
        copy /Y libs\SDL3-3.4.8\lib\x64\SDL3.dll build\ >nul 2>&1
        copy /Y libs\SDL3_ttf-3.2.2\lib\x64\SDL3_ttf.dll build\ >nul 2>&1
        copy /Y libs\SDL3_image-3.4.4\lib\x64\SDL3_image.dll build\ >nul 2>&1
        copy /Y libs\SDL3_mixer-3.2.2\lib\x64\SDL3_mixer.dll build\ >nul 2>&1
        if exist fonts\JetBrainsMono-Regular.ttf (
            copy /Y fonts\JetBrainsMono-Regular.ttf build\jdbasic_default.ttf >nul 2>&1
        )
        echo DLLs copied to build\
    )
    if defined COPY_OPENSSL (
        if exist "%OPENSSL%\bin\libssl-3-x64.dll" (
            copy /Y "%OPENSSL%\bin\libssl-3-x64.dll" build\ >nul 2>&1
            copy /Y "%OPENSSL%\bin\libcrypto-3-x64.dll" build\ >nul 2>&1
            echo OpenSSL DLLs copied to build\
        ) else (
            echo WARNING: OpenSSL DLLs not found at %OPENSSL%\bin
        )
    )
    if exist libs\onnxruntime\lib\onnxruntime.dll (
        copy /Y libs\onnxruntime\lib\onnxruntime.dll build\ >nul 2>&1
        copy /Y libs\onnxruntime\lib\onnxruntime_providers_shared.dll build\ >nul 2>&1
        echo ONNX Runtime DLL copied to build\
    )
    if defined COPY_LLVM (
        copy /Y libs\LLVM\bin\LLVM-C.dll build\ >nul 2>&1
        echo LLVM DLL copied to build\
        REM Pre-compile jdb_runtime.obj for linking into generated executables.
        REM Pass the feature DEFS so jdb_os_feature reports the build's flags.
        "%CC%" /std:c++17 /O2 /EHsc /c !DEFS! src\jdb_runtime.cpp ^
            "/I%MSVC%\include" "/I%SDK%\Include\%SDKV%\ucrt" ^
            "/I%SDK%\Include\%SDKV%\um" "/I%SDK%\Include\%SDKV%\shared" ^
            /Isrc /Fo:build\jdb_runtime.obj
        if errorlevel 1 (
            echo BUILD FAILED: jdb_runtime.obj
        ) else (
            echo jdb_runtime.obj compiled for native linking
        )
    )
    if exist libs\llama\llama.dll (
        copy /Y libs\llama\llama.dll build\ >nul 2>&1
        copy /Y libs\llama\ggml.dll build\ >nul 2>&1
        copy /Y libs\llama\ggml-base.dll build\ >nul 2>&1
        copy /Y libs\llama\ggml-cpu*.dll build\ >nul 2>&1
        copy /Y libs\llama\ggml-cuda.dll build\ >nul 2>&1
        copy /Y libs\llama\cublas64_12.dll build\ >nul 2>&1
        copy /Y libs\llama\cublasLt64_12.dll build\ >nul 2>&1
        copy /Y libs\llama\cudart64_12.dll build\ >nul 2>&1
        echo LLM DLLs copied to build\
    )
    if defined WANT_PYTHON (
        REM Bundle the CPython runtime DLL so the exe loads without relying on
        REM PATH. The standard library is found via PYHOME at runtime.
        copy /Y "!PYHOME!\python314.dll" build\ >nul 2>&1
        copy /Y "!PYHOME!\python3.dll" build\ >nul 2>&1
        echo Python DLL copied to build\
    )
) else (
    echo.
    echo BUILD FAILED
)

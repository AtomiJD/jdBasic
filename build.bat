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
set EXTRA_LIB=user32.lib ws2_32.lib iphlpapi.lib
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
        set EXTRA_SRC=!EXTRA_SRC! src\graphics.cpp src\tiledmap.cpp
        set EXTRA_INC=!EXTRA_INC! /Ilibs\SDL3-3.2.16\include /Ilibs\SDL3_ttf-3.2.2\include /Ilibs\SDL3_image-3.2.4\include /Ilibs\SDL2_mixer-2.8.1\include
        set EXTRA_LIBPATH=!EXTRA_LIBPATH! /LIBPATH:libs\SDL3-3.2.16\lib\x64 /LIBPATH:libs\SDL3_ttf-3.2.2\lib\x64 /LIBPATH:libs\SDL3_image-3.2.4\lib\x64 /LIBPATH:libs\SDL2_mixer-2.8.1\lib\x64
        set EXTRA_LIB=!EXTRA_LIB! SDL3.lib SDL3_ttf.lib SDL3_image.lib SDL2_mixer.lib
        set COPY_DLLS=1
        echo [+] Graphics/Audio - SDL3 + TTF + Image + Mixer
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
    if /I "%%A"=="RELEASE" (
        REM Increment build number
        set /p BNUM=< %BUILD_NUM_FILE%
        set /a BNUM=!BNUM!+1
        echo !BNUM!> %BUILD_NUM_FILE%
        REM Get date/time
        set BDATE=%date:~6,4%/%date:~3,2%/%date:~0,2%-%time:~0,2%:%time:~3,2%.%time:~6,2%
        set BDATE=!BDATE: =0!
        set DEFS=!DEFS! /DJDBASIC_BUILD_NUM=\"!BNUM!\" /DJDBASIC_BUILD_DATE=\"!BDATE!\"
        echo [+] RELEASE Build !BNUM! - !BDATE!
    )
)

REM Compile resources (icon, manifest, version info)
set "RC=%SDK%\bin\%SDKV%\x64\rc.exe"
"%RC%" /fo build\version.res resources\version.rc >nul 2>&1

"%CC%" /std:c++17 /O2 /EHa %DEFS% ^
  /I"%MSVC%\include" ^
  /I"%SDK%\Include\%SDKV%\ucrt" ^
  /I"%SDK%\Include\%SDKV%\um" ^
  /I"%SDK%\Include\%SDKV%\shared" ^
  /Isrc !EXTRA_INC! ^
  src\main.cpp src\lexer.cpp src\parser.cpp src\compiler.cpp src\vm.cpp src\console.cpp src\editor.cpp src\dap.cpp src\ffi.cpp src\sound.cpp src\gui.cpp src\ai.cpp src\llm.cpp !EXTRA_SRC! ^
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
    if defined COPY_DLLS (
        copy /Y libs\SDL3-3.2.16\lib\x64\SDL3.dll build\ >nul 2>&1
        copy /Y libs\SDL3_ttf-3.2.2\lib\x64\SDL3_ttf.dll build\ >nul 2>&1
        copy /Y libs\SDL3_image-3.2.4\lib\x64\SDL3_image.dll build\ >nul 2>&1
        copy /Y libs\SDL2_mixer-2.8.1\lib\x64\SDL2_mixer.dll build\ >nul 2>&1
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
        REM Pre-compile jdb_runtime.obj for linking into generated executables
        "%CC%" /std:c++17 /O2 /EHsc /c src\jdb_runtime.cpp /Fo:build\jdb_runtime.obj >nul 2>&1
        echo jdb_runtime.obj compiled for native linking
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
) else (
    echo.
    echo BUILD FAILED
)

@echo off
setlocal enabledelayedexpansion

REM ──────────────────────────────────────────────────────────────────
REM dist.bat — build jdBasic with a chosen feature set and assemble
REM            a clean redistributable directory in dist\jdBasic\.
REM
REM Usage:   dist.bat [FEATURES...]
REM
REM Features (same names as build.bat):
REM   COM HTTP SERIAL GFX IMGUI LLM ONNX
REM Extras:
REM   NOCUDA   - skip the large CUDA DLLs even when LLM is selected
REM   NOBUILD  - skip the compile step (just repackage build\)
REM   CLEAN    - delete dist\jdBasic before assembling
REM
REM Example: dist.bat GFX IMGUI HTTP NOCUDA
REM ──────────────────────────────────────────────────────────────────

set OPENSSL=C:\Program Files\OpenSSL-Win64
set DIST=dist\jdBasic
set BUILD_FLAGS=
set WANT_GFX=
set WANT_HTTP=
set WANT_LLM=
set WANT_ONNX=
set NOCUDA=
set NOBUILD=
set CLEAN=

REM Parse arguments
for %%A in (%*) do (
    if /I "%%A"=="NOCUDA" ( set NOCUDA=1
    ) else if /I "%%A"=="NOBUILD" ( set NOBUILD=1
    ) else if /I "%%A"=="CLEAN" ( set CLEAN=1
    ) else (
        set BUILD_FLAGS=!BUILD_FLAGS! %%A
        if /I "%%A"=="GFX"   set WANT_GFX=1
        if /I "%%A"=="HTTP"  set WANT_HTTP=1
        if /I "%%A"=="LLM"   set WANT_LLM=1
        if /I "%%A"=="ONNX"  set WANT_ONNX=1
    )
)

if "%BUILD_FLAGS%"=="" (
    echo Usage: dist.bat [FEATURES...] [NOCUDA] [NOBUILD] [CLEAN]
    echo Features: COM HTTP SERIAL GFX IMGUI LLM ONNX
    exit /b 1
)

REM ── 1) Build ────────────────────────────────────────────────────
if not defined NOBUILD (
    echo === Building jdBasic with:%BUILD_FLAGS% ===
    call build.bat%BUILD_FLAGS%
    if errorlevel 1 (
        echo BUILD FAILED — aborting dist
        exit /b 1
    )
)

if not exist build\jdBasic.exe (
    echo build\jdBasic.exe not found — run build.bat first or omit NOBUILD
    exit /b 1
)

REM ── 2) Prepare dist directory ───────────────────────────────────
if defined CLEAN (
    if exist %DIST% rmdir /s /q %DIST%
)
if not exist dist mkdir dist
if not exist %DIST% mkdir %DIST%

REM ── 3) Copy EXE ─────────────────────────────────────────────────
echo === Assembling %DIST% ===
copy /Y build\jdBasic.exe %DIST%\ >nul

REM ── 4) Copy feature-specific DLLs ───────────────────────────────
if defined WANT_GFX (
    copy /Y build\SDL3.dll        %DIST%\ >nul
    copy /Y build\SDL3_ttf.dll    %DIST%\ >nul
    copy /Y build\SDL3_image.dll  %DIST%\ >nul
    copy /Y build\SDL2_mixer.dll  %DIST%\ >nul
    echo  [+] SDL3 / TTF / Image / Mixer
)

if defined WANT_HTTP (
    if exist build\libssl-3-x64.dll (
        copy /Y build\libssl-3-x64.dll    %DIST%\ >nul
        copy /Y build\libcrypto-3-x64.dll %DIST%\ >nul
        echo  [+] OpenSSL ^(libssl-3, libcrypto-3^)
    ) else if exist "%OPENSSL%\bin\libssl-3-x64.dll" (
        copy /Y "%OPENSSL%\bin\libssl-3-x64.dll"    %DIST%\ >nul
        copy /Y "%OPENSSL%\bin\libcrypto-3-x64.dll" %DIST%\ >nul
        echo  [+] OpenSSL ^(libssl-3, libcrypto-3^)
    ) else (
        echo  [!] WARNING: OpenSSL DLLs not found
        echo      Target machine must have OpenSSL installed.
    )
)

if defined WANT_ONNX (
    copy /Y build\onnxruntime.dll                  %DIST%\ >nul
    copy /Y build\onnxruntime_providers_shared.dll %DIST%\ >nul
    echo  [+] ONNX Runtime
)

if defined WANT_LLM (
    copy /Y build\llama.dll        %DIST%\ >nul
    copy /Y build\ggml.dll         %DIST%\ >nul
    copy /Y build\ggml-base.dll    %DIST%\ >nul
    copy /Y build\ggml-cpu-*.dll   %DIST%\ >nul
    echo  [+] llama.cpp ^(CPU backends^)
    if not defined NOCUDA (
        if exist build\ggml-cuda.dll (
            copy /Y build\ggml-cuda.dll    %DIST%\ >nul
            copy /Y build\cudart64_12.dll  %DIST%\ >nul
            copy /Y build\cublas64_12.dll  %DIST%\ >nul
            copy /Y build\cublasLt64_12.dll %DIST%\ >nul
            echo  [+] CUDA backend
        )
    ) else (
        echo  [-] CUDA skipped ^(NOCUDA^)
    )
)

REM ── 5) Summary ──────────────────────────────────────────────────
echo.
echo === DIST OK: %DIST% ===
for /f %%S in ('dir /a-d /s /-c %DIST% ^| findstr /C:"File(s)"') do echo %%S
echo.
echo Target machine requirements:
echo  - Visual C++ 2015-2022 Redistributable ^(x64^)
echo    https://aka.ms/vs/17/release/vc_redist.x64.exe
echo.

endlocal

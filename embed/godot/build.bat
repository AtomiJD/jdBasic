@echo off
REM Build jdb_godot.dll (the GDExtension) and stage it + jdbrt.dll into
REM godot\jd-one\addons\jdb_godot\bin\ so the demo project picks it up.
REM
REM First run: godot-cpp's bindings library compiles too (~5-10 min).
REM Subsequent incremental builds finish in seconds.
REM
REM Run from this folder: embed\godot\build.bat [target]
REM   target = template_debug (default) or template_release

setlocal
set REPO=%~dp0..\..
set SCONS=C:\Users\atomi\AppData\Roaming\Python\Python314\Scripts\scons.exe

set TARGET=%1
if "%TARGET%"=="" set TARGET=template_debug

REM Make sure jdbrt.dll exists. Prefer the HEADLESS build (no SDL / ImGui /
REM OpenSSL inside the host process) - that's the right flavour for a Godot
REM embed. If the user has a non-HEADLESS jdbrt sitting in build/ from
REM another flow, fall back to copying that.
if not exist "%REPO%\build\jdbrt.dll" (
    echo [info] no jdbrt.dll in build/ - running build_rt.bat HEADLESS ...
    pushd "%REPO%"
    call build_rt.bat HEADLESS
    popd
)

echo Building jdb_godot.dll (target=%TARGET%) ...
echo (first run compiles godot-cpp bindings -- this can take 5-10 min)
echo.

pushd "%~dp0"
"%SCONS%" platform=windows target=%TARGET% arch=x86_64
set RC=%ERRORLEVEL%
popd

if %RC% NEQ 0 (
    echo SCons exit %RC%
    exit /b %RC%
)

echo.
echo Staging jdbrt.dll + satellite DLLs into the demo project ...
set DST=%REPO%\godot\jd-one\addons\jdb_godot\bin
REM jdbrt.dll plus whatever satellite DLLs build_rt.bat dropped in build\
REM (LLM: llama.dll / ggml*.dll / cuda*.dll). The recommended embed flavour
REM is `build_rt.bat LLM SOUND HTTP` - SOUND_DSP pulls no device and HTTP
REM links OpenSSL, so we also stage the two OpenSSL runtime DLLs below.
REM Never build GFX / IMGUI / OPENGL for the embed - those pull SDL3 / LLVM-C
REM which aren't here and Godot then fails to load jdb_godot with error 126.
copy /Y "%REPO%\build\*.dll" "%DST%" >nul
set OSSL=C:\Program Files\OpenSSL-Win64\bin
if exist "%OSSL%\libssl-3-x64.dll"    copy /Y "%OSSL%\libssl-3-x64.dll"    "%DST%" >nul
if exist "%OSSL%\libcrypto-3-x64.dll" copy /Y "%OSSL%\libcrypto-3-x64.dll" "%DST%" >nul

echo BUILD OK: %DST%
dir /b "%DST%"

endlocal

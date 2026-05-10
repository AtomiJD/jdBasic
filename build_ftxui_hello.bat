@echo off
setlocal enabledelayedexpansion

REM Compile + link tests\ftxui_hello.cpp against libs\ftxui\build\ftxui.lib.
REM Validates that the static-lib ABI matches the rest of our toolchain
REM (same MSVC version, /MD runtime, UNICODE mode).

pushd "%~dp0"

set MSVC=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.44.35207
set SDK=C:\Program Files (x86)\Windows Kits\10
set SDKV=10.0.26100.0
set "CC=%MSVC%\bin\Hostx64\x64\cl.exe"

if not exist libs\ftxui\build\ftxui.lib (
    echo libs\ftxui\build\ftxui.lib missing. Run build_ftxui.bat first.
    exit /b 1
)
if not exist build mkdir build

echo === ftxui_hello: compile + link ===
"%CC%" /nologo /std:c++17 /O2 /EHsc /MD /DUNICODE /D_UNICODE ^
  /I"%MSVC%\include" /I"%SDK%\Include\%SDKV%\ucrt" /I"%SDK%\Include\%SDKV%\um" /I"%SDK%\Include\%SDKV%\shared" ^
  /Ilibs\ftxui\include ^
  tests\ftxui_hello.cpp ^
  /Fe:build\ftxui_hello.exe /Fo:build\ ^
  /link ^
  /LIBPATH:"%MSVC%\lib\x64" /LIBPATH:"%SDK%\Lib\%SDKV%\ucrt\x64" /LIBPATH:"%SDK%\Lib\%SDKV%\um\x64" ^
  libs\ftxui\build\ftxui.lib ^
  user32.lib

if errorlevel 1 (
    echo BUILD FAILED.
    exit /b 1
)

echo BUILD OK: build\ftxui_hello.exe
popd
endlocal

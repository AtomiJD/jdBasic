@echo off
REM Build embed\godot\tests\test_embed.exe and copy jdbrt.dll alongside it.
REM Run from the repo root: embed\godot\tests\build_test_embed.bat

setlocal
set REPO=%~dp0..\..\..
set MSVC=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.44.35207
set SDK=C:\Program Files (x86)\Windows Kits\10
set SDKV=10.0.26100.0
set "CC=%MSVC%\bin\Hostx64\x64\cl.exe"

echo Building test_embed.exe ...

"%CC%" /nologo /std:c11 /O2 /MD ^
  /I"%REPO%\src" ^
  /I"%MSVC%\include" ^
  /I"%SDK%\Include\%SDKV%\ucrt" ^
  /I"%SDK%\Include\%SDKV%\um" ^
  /I"%SDK%\Include\%SDKV%\shared" ^
  "%~dp0test_embed.c" ^
  /Fe:"%~dp0test_embed.exe" ^
  /Fo:"%~dp0test_embed.obj" ^
  /link ^
  /LIBPATH:"%MSVC%\lib\x64" ^
  /LIBPATH:"%SDK%\Lib\%SDKV%\ucrt\x64" ^
  /LIBPATH:"%SDK%\Lib\%SDKV%\um\x64" ^
  /LIBPATH:"%REPO%\build" ^
  jdbrt.lib

if %ERRORLEVEL% NEQ 0 (
    echo BUILD FAILED
    exit /b 1
)

REM Copy the runtime DLL next to the test exe so it finds jdbrt + sat DLLs.
copy /Y "%REPO%\build\jdbrt.dll"     "%~dp0" >nul
copy /Y "%REPO%\build\SDL3.dll"      "%~dp0" >nul 2>&1
copy /Y "%REPO%\build\SDL3_ttf.dll"  "%~dp0" >nul 2>&1
copy /Y "%REPO%\build\SDL3_image.dll" "%~dp0" >nul 2>&1
copy /Y "%REPO%\build\SDL3_mixer.dll" "%~dp0" >nul 2>&1
copy /Y "%REPO%\build\LLVM-C.dll"    "%~dp0" >nul 2>&1

echo BUILD OK: test_embed.exe (jdbrt.dll copied alongside)
endlocal

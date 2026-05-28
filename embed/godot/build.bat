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
echo Staging jdbrt.dll + sat DLLs into the demo project ...
set DST=%REPO%\godot\jd-one\addons\jdb_godot\bin
copy /Y "%REPO%\build\jdbrt.dll"      "%DST%" >nul
copy /Y "%REPO%\build\SDL3.dll"       "%DST%" >nul 2>&1
copy /Y "%REPO%\build\SDL3_ttf.dll"   "%DST%" >nul 2>&1
copy /Y "%REPO%\build\SDL3_image.dll" "%DST%" >nul 2>&1
copy /Y "%REPO%\build\SDL3_mixer.dll" "%DST%" >nul 2>&1
copy /Y "%REPO%\build\LLVM-C.dll"     "%DST%" >nul 2>&1

echo BUILD OK: %DST%
dir /b "%DST%"

endlocal

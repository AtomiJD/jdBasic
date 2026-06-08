@echo off
setlocal
REM ============================================================
REM Build screencap.dll for jdBasic FFI (GDI screen/window capture).
REM
REM Requirements:
REM   - cl.exe in PATH (run from a "x64 Native Tools" cmd prompt)
REM
REM The built DLL is copied to the repo root next to jdBasic.exe so
REM jdBasic can find it via LoadLibrary's default search path.
REM ============================================================

cd /d "%~dp0"

cl /nologo /O2 /MD /LD /W3 ^
   screencap.c ^
   /Fe:screencap.dll ^
   /link gdi32.lib user32.lib
if errorlevel 1 (
    echo ERROR: build failed
    exit /b 1
)

copy /y screencap.dll ..\..\ > nul
echo.
echo Built and deployed screencap.dll to %CD%\..\..\

endlocal

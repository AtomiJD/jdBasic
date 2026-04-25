@echo off
setlocal
REM ============================================================
REM Build sqlitebridge.dll for jdBasic FFI.
REM
REM Requirements:
REM   - cl.exe in PATH (run from a "x64 Native Tools" cmd or VS Developer Prompt)
REM   - sqlite3.c and sqlite3.h next to this script
REM     Download from: https://sqlite.org/download.html
REM     (file: sqlite-amalgamation-XXXXXXX.zip)
REM
REM The built DLL is copied to the repo root next to jdBasic.exe so
REM jdBasic can find it via LoadLibrary's default search path.
REM ============================================================

cd /d "%~dp0"

if not exist sqlite3.c (
    echo ERROR: sqlite3.c not found in %CD%
    echo.
    echo Download the SQLite amalgamation from
    echo   https://sqlite.org/download.html
    echo and extract sqlite3.c + sqlite3.h next to build.bat.
    exit /b 1
)
if not exist sqlite3.h (
    echo ERROR: sqlite3.h not found in %CD%
    exit /b 1
)

cl /nologo /O2 /MD /LD /W3 ^
   /DSQLITE_THREADSAFE=1 ^
   /DSQLITE_ENABLE_FTS5 ^
   sqlitebridge.c sqlite3.c ^
   /Fe:sqlitebridge.dll
if errorlevel 1 (
    echo ERROR: build failed
    exit /b 1
)

REM Copy the DLL next to the interpreter so LoadLibraryA finds it.
copy /y sqlitebridge.dll ..\..\ > nul
echo.
echo Built and deployed sqlitebridge.dll to %CD%\..\..\

endlocal

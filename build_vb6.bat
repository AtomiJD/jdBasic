@echo off
setlocal enabledelayedexpansion

REM build_vb6.bat - VB6-style desktop release bundle for Windows.
REM Wraps build.bat MCPSERVER HTTP NET GFX IMGUI COM FORMS SQLITE NATIVEC
REM RELEASE and build_rt.bat with the same feature set, then assembles a
REM redistributable zip in release\. Ships native Win32 forms (FORM.*), COM
REM automation, embedded SQLite, the MCP server and the LLVM-18 native
REM backend, so a forms program compiles to a standalone .exe.

REM Make the script robust against being called from any working directory.
pushd "%~dp0"

set BUNDLE=jdbasic-vb6-windows-x64
set OUT=release\%BUNDLE%
set ZIP=release\%BUNDLE%.zip

echo === build_vb6: compile EXE (MCPSERVER HTTP NET GFX IMGUI COM FORMS SQLITE NATIVEC RELEASE) ===
call .\build.bat MCPSERVER HTTP NET GFX IMGUI COM FORMS SQLITE NATIVEC RELEASE
if errorlevel 1 (
    echo EXE BUILD FAILED - bundle not assembled.
    exit /b 1
)
if not exist build\jdBasic.exe (
    echo build\jdBasic.exe missing - bundle not assembled.
    exit /b 1
)

echo.
echo === build_vb6: build runtime DLL (COM HTTP NET GFX IMGUI FORMS SQLITE) ===
call .\build_rt.bat COM HTTP NET GFX IMGUI FORMS SQLITE
if errorlevel 1 (
    echo RT BUILD FAILED - bundle not assembled.
    exit /b 1
)
if not exist build\jdbrt.dll (
    echo build\jdbrt.dll missing after build_rt - bundle not assembled.
    exit /b 1
)

echo.
echo === build_vb6: assemble %OUT% ===
if exist "%OUT%" rmdir /S /Q "%OUT%"
mkdir "%OUT%"
mkdir "%OUT%\doc"
mkdir "%OUT%\demos"

copy /Y build\jdBasic.exe          "%OUT%\" >nul

REM Native backend: -c links the LLVM-emitted object against jdb_runtime.obj
REM and jdbrt.lib, and the .exe it writes loads jdbrt.dll at runtime.
copy /Y build\jdbrt.dll            "%OUT%\" >nul
copy /Y build\jdbrt.lib            "%OUT%\" >nul
copy /Y build\jdb_runtime.obj      "%OUT%\" >nul
copy /Y build\LLVM-C.dll           "%OUT%\" >nul
copy /Y build\SDL3.dll             "%OUT%\" >nul 2>&1
copy /Y build\SDL3_ttf.dll         "%OUT%\" >nul 2>&1
copy /Y build\SDL3_image.dll       "%OUT%\" >nul 2>&1
copy /Y build\SDL3_mixer.dll       "%OUT%\" >nul 2>&1
copy /Y build\libssl-3-x64.dll     "%OUT%\" >nul 2>&1
copy /Y build\libcrypto-3-x64.dll  "%OUT%\" >nul 2>&1

REM Default font for GFX TEXT - looked up next to the EXE at runtime.
copy /Y build\jdbasic_default.ttf  "%OUT%\" >nul

REM Docs that jdb_doc reads at runtime + project-level licence.
copy /Y LICENSE.txt              "%OUT%\"     >nul
copy /Y THIRD_PARTY_LICENSES.txt "%OUT%\"     >nul
copy /Y doc\MCP.md               "%OUT%\doc\" >nul
copy /Y doc\languages.md         "%OUT%\doc\" >nul

REM Ship help.txt so the in-REPL HELP command and --dump-help work next to the exe.
copy /Y help.txt "%OUT%\" >nul

REM Forms showcase: sources + designer layouts, no binaries.
copy /Y jdb\demos\forms\forms_demo.jdb "%OUT%\demos\" >nul
copy /Y jdb\demos\forms\gallery.jdb    "%OUT%\demos\" >nul
copy /Y jdb\demos\forms\mdi_demo.jdb   "%OUT%\demos\" >nul
copy /Y jdb\demos\forms\mdi_doc.jdform "%OUT%\demos\" >nul
copy /Y jdb\demos\forms\tasklist.jdb   "%OUT%\demos\" >nul
copy /Y jdb\demos\forms\tasklist.jdform "%OUT%\demos\" >nul

REM Module library and the examples, graphics and ImGui included.
call .\release_content.bat "%OUT%" GFX
if errorlevel 1 (
    echo CONTENT COPY FAILED - bundle not assembled.
    exit /b 1
)

REM App-local VC++ runtime so the bundle runs on a clean Windows without the redist installed.
copy /Y "%SystemRoot%\System32\vcruntime140.dll"   "%OUT%\" >nul
copy /Y "%SystemRoot%\System32\vcruntime140_1.dll" "%OUT%\" >nul
copy /Y "%SystemRoot%\System32\msvcp140.dll"       "%OUT%\" >nul

REM --- BUILD_INFO.txt: stamp the current build number + date ---
set /p BNUM=<build_number.txt
set BDATE=%date:~6,4%/%date:~3,2%/%date:~0,2%
> "%OUT%\BUILD_INFO.txt" echo jdBasic VB6 Pack - Build !BNUM! - !BDATE!
>>"%OUT%\BUILD_INFO.txt" echo.
>>"%OUT%\BUILD_INFO.txt" echo Binary: jdBasic.exe ^(Build !BNUM!, !BDATE!^)
>>"%OUT%\BUILD_INFO.txt" echo Features: Forms, COM, SQLite, HTTP, NET, GFX, ImGui, MCP, NativeC
>>"%OUT%\BUILD_INFO.txt" echo Build flags: MCPSERVER HTTP NET GFX IMGUI COM FORMS SQLITE NATIVEC

REM End-user README inside the bundle.
> "%OUT%\README.txt" echo jdBasic VB6 Pack ^(Windows x64^)
>>"%OUT%\README.txt" echo.
>>"%OUT%\README.txt" echo Classic VB6-style desktop development, reborn:
>>"%OUT%\README.txt" echo   - FORM.* : native Win32 windows, buttons, lists, grids,
>>"%OUT%\README.txt" echo     menus, dialogs - event-driven like VB6.
>>"%OUT%\README.txt" echo   - COM automation: drive Excel, Word, Outlook, WScript
>>"%OUT%\README.txt" echo     ^(CREATEOBJECT^).
>>"%OUT%\README.txt" echo   - SQLite embedded ^(SQL.*^) - no server, one file.
>>"%OUT%\README.txt" echo   - Plus the full jdBasic core: GFX, ImGui, HTTP.
>>"%OUT%\README.txt" echo   - Native compiler: jdBasic.exe -c app.jdb writes a
>>"%OUT%\README.txt" echo     standalone .exe, forms and all.
>>"%OUT%\README.txt" echo.
>>"%OUT%\README.txt" echo Try the demos:
>>"%OUT%\README.txt" echo     jdBasic.exe demos\gallery.jdb     - every control at a glance
>>"%OUT%\README.txt" echo     jdBasic.exe demos\forms_demo.jdb  - forms basics
>>"%OUT%\README.txt" echo     jdBasic.exe demos\tasklist.jdb    - designer-built app ^(.jdform^)
>>"%OUT%\README.txt" echo     jdBasic.exe demos\mdi_demo.jdb    - MDI editor
>>"%OUT%\README.txt" echo.
>>"%OUT%\README.txt" echo More example programs: see examples\README.txt.
>>"%OUT%\README.txt" echo Modules for IMPORT live in lib\ and are found next to the EXE.
>>"%OUT%\README.txt" echo.
>>"%OUT%\README.txt" echo The .jdform layouts are made with the jdBasic VS Code
>>"%OUT%\README.txt" echo extension's visual form designer.
>>"%OUT%\README.txt" echo.
>>"%OUT%\README.txt" echo Compile a program to a standalone .exe:
>>"%OUT%\README.txt" echo     jdBasic.exe -c demos\tasklist.jdb
>>"%OUT%\README.txt" echo.
>>"%OUT%\README.txt" echo -c copies jdbrt.dll and jdbasic_default.ttf next to the
>>"%OUT%\README.txt" echo .exe it writes. The native compiler is always STRICT and
>>"%OUT%\README.txt" echo EXPLICIT, so every variable needs a DIM. All four demos
>>"%OUT%\README.txt" echo above compile as they are.
>>"%OUT%\README.txt" echo.
>>"%OUT%\README.txt" echo NATIVE COMPILE REQUIREMENTS:
>>"%OUT%\README.txt" echo     -c invokes the MSVC linker to fuse the LLVM-emitted
>>"%OUT%\README.txt" echo     object with jdb_runtime.obj + jdbrt.lib. You need
>>"%OUT%\README.txt" echo     Visual Studio 2022 17.10 or newer ^(Community is fine^)
>>"%OUT%\README.txt" echo     with the "Desktop development with C++" workload and
>>"%OUT%\README.txt" echo     a Windows 10 SDK. Older MSVC fails at link time with
>>"%OUT%\README.txt" echo     LNK2019 on __std_find_trivial_1, because
>>"%OUT%\README.txt" echo     jdb_runtime.obj uses STL helpers added in MSVC v14.40.
>>"%OUT%\README.txt" echo     Running scripts interpreted needs none of this.
>>"%OUT%\README.txt" echo.
>>"%OUT%\README.txt" echo Run as MCP stdio server ^(for AI coding agents^):
>>"%OUT%\README.txt" echo     jdBasic.exe --mcp
>>"%OUT%\README.txt" echo.
>>"%OUT%\README.txt" echo Language reference: doc\languages.md ^(FORM chapter^).
>>"%OUT%\README.txt" echo MCP client config and tool reference: doc\MCP.md.
>>"%OUT%\README.txt" echo Source / issues: https://github.com/AtomiJD/jdBasic

echo.
echo === build_vb6: zip ===
if exist "%ZIP%" del "%ZIP%"
powershell -NoProfile -Command "Compress-Archive -Path '%OUT%\*' -DestinationPath '%ZIP%' -Force"
if errorlevel 1 (
    echo ZIP FAILED.
    exit /b 1
)

for %%I in ("%ZIP%") do set ZIPSIZE=%%~zI
powershell -NoProfile -Command "(Get-FileHash -Algorithm SHA256 '%ZIP%').Hash" > "%ZIP%.sha256"
set /p HASH=<"%ZIP%.sha256"

echo.
echo BUNDLE OK
echo   %ZIP%   (%ZIPSIZE% bytes)
echo   sha256  %HASH%
popd
endlocal

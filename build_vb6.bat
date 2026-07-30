@echo off
setlocal enabledelayedexpansion

REM build_vb6.bat - VB6-style desktop release bundle for Windows.
REM Wraps build.bat MCPSERVER HTTP GFX IMGUI COM FORMS SQLITE RELEASE, then
REM assembles a redistributable zip in release\. Ships native Win32 forms
REM (FORM.*), COM automation, embedded SQLite and the MCP server. No NATIVEC -
REM scripts run interpreted, so there is no jdbrt.dll in this pack.

REM Make the script robust against being called from any working directory.
pushd "%~dp0"

set BUNDLE=jdbasic-vb6-windows-x64
set OUT=release\%BUNDLE%
set ZIP=release\%BUNDLE%.zip

echo === build_vb6: compile EXE (MCPSERVER HTTP GFX IMGUI COM FORMS SQLITE RELEASE) ===
call .\build.bat MCPSERVER HTTP GFX IMGUI COM FORMS SQLITE RELEASE
if errorlevel 1 (
    echo EXE BUILD FAILED - bundle not assembled.
    exit /b 1
)
if not exist build\jdBasic.exe (
    echo build\jdBasic.exe missing - bundle not assembled.
    exit /b 1
)

echo.
echo === build_vb6: assemble %OUT% ===
if exist "%OUT%" rmdir /S /Q "%OUT%"
mkdir "%OUT%"
mkdir "%OUT%\doc"
mkdir "%OUT%\demos"

copy /Y build\jdBasic.exe          "%OUT%\" >nul
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

REM App-local VC++ runtime so the bundle runs on a clean Windows without the redist installed.
copy /Y "%SystemRoot%\System32\vcruntime140.dll"   "%OUT%\" >nul
copy /Y "%SystemRoot%\System32\vcruntime140_1.dll" "%OUT%\" >nul
copy /Y "%SystemRoot%\System32\msvcp140.dll"       "%OUT%\" >nul

REM ── BUILD_INFO.txt: stamp the current build number + date ──
set /p BNUM=<build_number.txt
set BDATE=%date:~6,4%/%date:~3,2%/%date:~0,2%
> "%OUT%\BUILD_INFO.txt" echo jdBasic VB6 Pack - Build !BNUM! - !BDATE!
>>"%OUT%\BUILD_INFO.txt" echo.
>>"%OUT%\BUILD_INFO.txt" echo Binary: jdBasic.exe ^(Build !BNUM!, !BDATE!^)
>>"%OUT%\BUILD_INFO.txt" echo Features: Forms, COM, SQLite, HTTP, GFX, ImGui, MCP
>>"%OUT%\BUILD_INFO.txt" echo Build flags: MCPSERVER HTTP GFX IMGUI COM FORMS SQLITE

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
>>"%OUT%\README.txt" echo.
>>"%OUT%\README.txt" echo Try the demos:
>>"%OUT%\README.txt" echo     jdBasic.exe demos\gallery.jdb     - every control at a glance
>>"%OUT%\README.txt" echo     jdBasic.exe demos\forms_demo.jdb  - forms basics
>>"%OUT%\README.txt" echo     jdBasic.exe demos\tasklist.jdb    - designer-built app ^(.jdform^)
>>"%OUT%\README.txt" echo     jdBasic.exe demos\mdi_demo.jdb    - MDI editor
>>"%OUT%\README.txt" echo.
>>"%OUT%\README.txt" echo The .jdform layouts are made with the jdBasic VS Code
>>"%OUT%\README.txt" echo extension's visual form designer.
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

@echo off
setlocal enabledelayedexpansion

REM build_mcp.bat - Core MCP release bundle for Windows.
REM Wraps build.bat MCPSERVER HTTP RELEASE, then assembles a redistributable
REM zip in release\. Full build (NATIVEC/GFX/IMGUI) uses build.bat directly.

REM Make the script robust against being called from any working directory.
pushd "%~dp0"

set BUNDLE=jdbasic-core-windows-x64
set OUT=release\%BUNDLE%
set ZIP=release\%BUNDLE%.zip

echo === build_mcp: compile (MCPSERVER HTTP RELEASE) ===
call .\build.bat MCPSERVER HTTP RELEASE
if errorlevel 1 (
    echo BUILD FAILED - bundle not assembled.
    exit /b 1
)
if not exist build\jdBasic.exe (
    echo build\jdBasic.exe missing - bundle not assembled.
    exit /b 1
)

echo.
echo === build_mcp: assemble %OUT% ===
if exist "%OUT%" rmdir /S /Q "%OUT%"
mkdir "%OUT%"

copy /Y build\jdBasic.exe         "%OUT%\" >nul
copy /Y build\libssl-3-x64.dll    "%OUT%\" >nul 2>&1
copy /Y build\libcrypto-3-x64.dll "%OUT%\" >nul 2>&1
copy /Y LICENSE.txt               "%OUT%\" >nul
copy /Y THIRD_PARTY_LICENSES.txt "%OUT%\" >nul

REM jdb_doc reads doc/languages.md relative to CWD - must ship inside the
REM bundle, layout-preserving, or jdb_doc returns "Cannot read doc/languages.md".
mkdir "%OUT%\doc"
copy /Y doc\MCP.md       "%OUT%\doc\" >nul
copy /Y doc\languages.md "%OUT%\doc\" >nul

REM Ship help.txt so the in-REPL HELP command and --dump-help work next to the exe.
copy /Y help.txt "%OUT%\" >nul

REM App-local VC++ runtime so the bundle runs on a clean Windows without the redist installed.
copy /Y "%SystemRoot%\System32\vcruntime140.dll"   "%OUT%\" >nul
copy /Y "%SystemRoot%\System32\vcruntime140_1.dll" "%OUT%\" >nul
copy /Y "%SystemRoot%\System32\msvcp140.dll"       "%OUT%\" >nul

REM End-user README inside the bundle (kept short on purpose).
> "%OUT%\README.txt" echo jdBasic Core (MCP server build)
>>"%OUT%\README.txt" echo.
>>"%OUT%\README.txt" echo Run as MCP stdio server:
>>"%OUT%\README.txt" echo     jdBasic.exe --mcp
>>"%OUT%\README.txt" echo.
>>"%OUT%\README.txt" echo Run as MCP HTTP server (loopback only by default):
>>"%OUT%\README.txt" echo     jdBasic.exe --mcp-http 7321
>>"%OUT%\README.txt" echo.
>>"%OUT%\README.txt" echo doc\languages.md ships next to the EXE on purpose -
>>"%OUT%\README.txt" echo jdb_doc looks there first regardless of working dir,
>>"%OUT%\README.txt" echo so no "cwd" config is required.
>>"%OUT%\README.txt" echo.
>>"%OUT%\README.txt" echo Full client-config and tool reference: see doc\MCP.md.
>>"%OUT%\README.txt" echo Source / issues: https://github.com/AtomiJD/jdBasic

echo.
echo === build_mcp: zip ===
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

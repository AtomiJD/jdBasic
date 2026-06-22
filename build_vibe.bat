@echo off
setlocal enabledelayedexpansion

REM build_vibe.bat - Vibe-Game-Pack release bundle for Windows.
REM Wraps build.bat GFX IMGUI HTTP MCPSERVER RELEASE, then REFRESHES the
REM redistributable bundle in release\ in place: the binary, the SDL3 / OpenSSL
REM DLLs, the runtime docs and BUILD_INFO.txt are regenerated, while the curated
REM content (games\, run-*.bat, start-claude.bat, QUICKSTART.md, setup_guide.md,
REM CLAUDE.md, README.txt) is left untouched. No NATIVEC - the games run
REM interpreted, so there is no jdbrt.dll in this pack.

REM Make the script robust against being called from any working directory.
pushd "%~dp0"

set BUNDLE=jdbasic-vibe-game-pack-windows-x64
set OUT=release\%BUNDLE%
set ZIP=release\%BUNDLE%.zip

echo === build_vibe: compile (GFX IMGUI HTTP MCPSERVER RELEASE) ===
call .\build.bat GFX IMGUI HTTP MCPSERVER RELEASE
if errorlevel 1 (
    echo BUILD FAILED - bundle not refreshed.
    exit /b 1
)
if not exist build\jdBasic.exe (
    echo build\jdBasic.exe missing - bundle not refreshed.
    exit /b 1
)

if not exist "%OUT%" (
    echo [!] %OUT% does not exist yet. This script REFRESHES an existing
    echo     curated pack - games, launchers, guides. Create those first,
    echo     then re-run. Aborting so nothing half-built ships.
    exit /b 1
)
if not exist "%OUT%\games" echo [!] WARNING: %OUT%\games not found - pack will ship without games.
if not exist "%OUT%\doc" mkdir "%OUT%\doc"

echo.
echo === build_vibe: refresh binaries + docs in %OUT% ===
copy /Y build\jdBasic.exe          "%OUT%\" >nul
copy /Y build\SDL3.dll             "%OUT%\" >nul 2>&1
copy /Y build\SDL3_ttf.dll         "%OUT%\" >nul 2>&1
copy /Y build\SDL3_image.dll       "%OUT%\" >nul 2>&1
copy /Y build\SDL3_mixer.dll       "%OUT%\" >nul 2>&1
copy /Y build\libssl-3-x64.dll     "%OUT%\" >nul 2>&1
copy /Y build\libcrypto-3-x64.dll  "%OUT%\" >nul 2>&1
copy /Y LICENSE.txt                "%OUT%\"     >nul
copy /Y doc\MCP.md                 "%OUT%\doc\" >nul
copy /Y doc\languages.md           "%OUT%\doc\" >nul

REM Ship help.txt so the in-REPL HELP command and --dump-help work next to the exe.
copy /Y help.txt "%OUT%\" >nul

REM App-local VC++ runtime so the bundle runs on a clean Windows without the redist installed.
copy /Y "%SystemRoot%\System32\vcruntime140.dll"   "%OUT%\" >nul
copy /Y "%SystemRoot%\System32\vcruntime140_1.dll" "%OUT%\" >nul
copy /Y "%SystemRoot%\System32\msvcp140.dll"       "%OUT%\" >nul

REM ── BUILD_INFO.txt: stamp the current build number + date ──
set /p BNUM=<build_number.txt
set BDATE=%date:~6,4%/%date:~3,2%/%date:~0,2%
> "%OUT%\BUILD_INFO.txt" echo jdBasic Vibe-Game Pack - Build !BNUM! - !BDATE!
>>"%OUT%\BUILD_INFO.txt" echo.
>>"%OUT%\BUILD_INFO.txt" echo Binary: jdBasic.exe ^(Build !BNUM!, !BDATE!^)
>>"%OUT%\BUILD_INFO.txt" echo Features: HTTP, GFX, ImGui, MCP
>>"%OUT%\BUILD_INFO.txt" echo Build flags: GFX IMGUI HTTP MCPSERVER
>>"%OUT%\BUILD_INFO.txt" echo.
>>"%OUT%\BUILD_INFO.txt" echo What's inside:
>>"%OUT%\BUILD_INFO.txt" echo   games\pacman\   - vibe_game.jdb ^(Pac-clone with sprites, sound, MCP-pause^)
>>"%OUT%\BUILD_INFO.txt" echo   games\shooter\  - space_shooter.jdb ^(Stellar Drift vector shooter^)
>>"%OUT%\BUILD_INFO.txt" echo   games\empty\    - empty_game.jdb ^(clean canvas for new games^)
>>"%OUT%\BUILD_INFO.txt" echo.
>>"%OUT%\BUILD_INFO.txt" echo For first-time setup see QUICKSTART.md. For the full walkthrough
>>"%OUT%\BUILD_INFO.txt" echo see setup_guide.md.
>>"%OUT%\BUILD_INFO.txt" echo.
>>"%OUT%\BUILD_INFO.txt" echo Sources:
>>"%OUT%\BUILD_INFO.txt" echo   https://github.com/AtomiJD/jdBasic
>>"%OUT%\BUILD_INFO.txt" echo   https://jdbasic.org

echo.
echo === build_vibe: zip ===
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
echo BUNDLE OK  (Build !BNUM!)
echo   %ZIP%   (!ZIPSIZE! bytes)
echo   sha256  !HASH!
popd
endlocal

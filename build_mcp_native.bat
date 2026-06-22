@echo off
setlocal enabledelayedexpansion

REM build_mcp_native.bat - MCP-native release bundle for Windows.
REM Wraps build.bat MCPSERVER HTTP NATIVEC RELEASE, then assembles a
REM redistributable zip in release\. Includes the LLVM-18 native
REM backend so jdb_run_native -c can produce EXEs out of the box.

REM Make the script robust against being called from any working directory.
pushd "%~dp0"

set BUNDLE=jdbasic-mcp-native-windows-x64
set OUT=release\%BUNDLE%
set ZIP=release\%BUNDLE%.zip

echo === build_mcp_native: compile EXE (MCPSERVER HTTP GFX IMGUI NATIVEC RELEASE) ===
call .\build.bat MCPSERVER HTTP GFX IMGUI NATIVEC RELEASE
if errorlevel 1 (
    echo EXE BUILD FAILED - bundle not assembled.
    exit /b 1
)
if not exist build\jdBasic.exe (
    echo build\jdBasic.exe missing - bundle not assembled.
    exit /b 1
)

echo.
echo === build_mcp_native: build runtime DLL (GFX IMGUI) ===
call .\build_rt.bat GFX IMGUI
if errorlevel 1 (
    echo RT BUILD FAILED - bundle not assembled.
    exit /b 1
)
if not exist build\jdbrt.dll (
    echo build\jdbrt.dll missing after build_rt - bundle not assembled.
    exit /b 1
)

echo.
echo === build_mcp_native: assemble %OUT% ===
if exist "%OUT%" rmdir /S /Q "%OUT%"
mkdir "%OUT%"
mkdir "%OUT%\doc"

REM Server-side runtime + compiler dependencies.
REM
REM jdb_runtime.obj + jdbrt.lib are needed at *user-side* link time:
REM `jdBasic.exe -c file.jdb` invokes link.exe to fuse the LLVM-emitted
REM .obj with the runtime object and import lib. Without these two the
REM link step fails (LNK1181: cannot open input file 'build\jdbrt.lib').
copy /Y build\jdBasic.exe          "%OUT%\" >nul
copy /Y build\jdbrt.dll            "%OUT%\" >nul
copy /Y build\jdbrt.lib            "%OUT%\" >nul
copy /Y build\jdb_runtime.obj      "%OUT%\" >nul
copy /Y build\LLVM-C.dll           "%OUT%\" >nul
copy /Y build\libssl-3-x64.dll     "%OUT%\" >nul 2>&1
copy /Y build\libcrypto-3-x64.dll  "%OUT%\" >nul 2>&1
REM SDL3 + ImGui DLLs needed by jdbrt.dll (and any compiled EXE that uses
REM SCREEN / RECT / SPRITE / etc.). Without these, GFX-using compiled
REM EXEs would exit 127 with "DLL not found".
copy /Y build\SDL3.dll             "%OUT%\" >nul 2>&1
copy /Y build\SDL3_ttf.dll         "%OUT%\" >nul 2>&1
copy /Y build\SDL3_image.dll       "%OUT%\" >nul 2>&1
copy /Y build\SDL3_mixer.dll       "%OUT%\" >nul 2>&1

REM Docs that jdb_doc reads at runtime + project-level licence.
copy /Y LICENSE.txt        "%OUT%\"     >nul
copy /Y doc\MCP.md         "%OUT%\doc\" >nul
copy /Y doc\languages.md   "%OUT%\doc\" >nul

REM Ship help.txt so the in-REPL HELP command and --dump-help work next to the exe.
copy /Y help.txt "%OUT%\" >nul

REM App-local VC++ runtime so the bundle runs on a clean Windows without the redist installed.
copy /Y "%SystemRoot%\System32\vcruntime140.dll"   "%OUT%\" >nul
copy /Y "%SystemRoot%\System32\vcruntime140_1.dll" "%OUT%\" >nul
copy /Y "%SystemRoot%\System32\msvcp140.dll"       "%OUT%\" >nul

REM End-user README inside the bundle.
> "%OUT%\README.txt" echo jdBasic MCP-native build (Windows x64)
>>"%OUT%\README.txt" echo.
>>"%OUT%\README.txt" echo Includes the LLVM-18 native compiler (~70 MB DLL) so
>>"%OUT%\README.txt" echo jdb_run_native can turn jdBasic source into a real
>>"%OUT%\README.txt" echo standalone .exe via `jdBasic.exe -c file.jdb`.
>>"%OUT%\README.txt" echo.
>>"%OUT%\README.txt" echo Run as MCP stdio server:
>>"%OUT%\README.txt" echo     jdBasic.exe --mcp
>>"%OUT%\README.txt" echo.
>>"%OUT%\README.txt" echo Run as MCP HTTP server (loopback only by default):
>>"%OUT%\README.txt" echo     jdBasic.exe --mcp-http 7321
>>"%OUT%\README.txt" echo.
>>"%OUT%\README.txt" echo Compile a script to a native EXE:
>>"%OUT%\README.txt" echo     jdBasic.exe -c file.jdb
>>"%OUT%\README.txt" echo     copy jdbrt.dll alongside file.exe ^(or PATH^)
>>"%OUT%\README.txt" echo.
>>"%OUT%\README.txt" echo NATIVE COMPILE REQUIREMENTS:
>>"%OUT%\README.txt" echo     -c invokes the MSVC linker to fuse the LLVM-emitted
>>"%OUT%\README.txt" echo     object with jdb_runtime.obj + jdbrt.lib. You need:
>>"%OUT%\README.txt" echo       - Visual Studio 2022 17.10 or newer ^(Community
>>"%OUT%\README.txt" echo         is fine^), MSVC v14.40+ with the "Desktop
>>"%OUT%\README.txt" echo         development with C++" workload installed.
>>"%OUT%\README.txt" echo         Older MSVC ^(VS2019, early VS2022^) fails at
>>"%OUT%\README.txt" echo         link time with LNK2019 unresolved symbols
>>"%OUT%\README.txt" echo         __std_find_trivial_1 / __std_find_last_of_*
>>"%OUT%\README.txt" echo         because jdb_runtime.obj uses the vectorized
>>"%OUT%\README.txt" echo         STL helpers introduced in MSVC v14.40.
>>"%OUT%\README.txt" echo       - Windows SDK 10 ^(any recent version^)
>>"%OUT%\README.txt" echo     The MCP server itself ^(jdb_eval / jdb_doc / etc.^)
>>"%OUT%\README.txt" echo     does NOT need MSVC - only the -c compile path does.
>>"%OUT%\README.txt" echo.
>>"%OUT%\README.txt" echo doc\languages.md is read by jdb_doc at runtime;
>>"%OUT%\README.txt" echo it's looked up next to the EXE first, so no "cwd"
>>"%OUT%\README.txt" echo configuration is required in your MCP client.
>>"%OUT%\README.txt" echo.
>>"%OUT%\README.txt" echo Full client-config and tool reference: see doc\MCP.md.
>>"%OUT%\README.txt" echo Source / issues: https://github.com/AtomiJD/jdBasic

echo.
echo === build_mcp_native: zip ===
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

@echo off
setlocal enabledelayedexpansion

REM build_full.bat - the Windows pack with every feature the Windows build
REM has, for the machines here: tools/install_bin.jdb installs it under
REM D:\usr\bin\jdbasic\<version> so hooks, the MCP and other repositories
REM run a released jdBasic instead of the build directory of this checkout.
REM It is not uploaded to GitHub.
REM
REM   build_full.bat          release build (bumps build_number.txt)
REM   build_full.bat DEV      the same without RELEASE, between two releases

pushd "%~dp0"

set BUNDLE=jdbasic-full-windows-x64
set OUT=release\%BUNDLE%
set ZIP=release\%BUNDLE%.zip
set FLAGS=HTTP NET COM GFX IMGUI NATIVEC MCPSERVER FORMS SERIAL SQLITE FX MIDI MINIAUDIO SOUND OPENGL TUI FTXUI LLM ONNX PYTHON
set RTFLAGS=HTTP NET COM GFX IMGUI FORMS SERIAL SQLITE FX MIDI MINIAUDIO SOUND OPENGL TUI FTXUI LLM ONNX PYTHON
set REL=RELEASE
if /I "%~1"=="DEV" set REL=

echo === build_full: compile EXE (%FLAGS% %REL%) ===
call .\build.bat %FLAGS% %REL%
if errorlevel 1 (
    echo EXE BUILD FAILED - bundle not assembled.
    exit /b 1
)
if not exist build\jdBasic.exe (
    echo build\jdBasic.exe missing - bundle not assembled.
    exit /b 1
)

echo.
echo === build_full: build runtime DLL (%RTFLAGS%) ===
call .\build_rt.bat %RTFLAGS%
if errorlevel 1 (
    echo RT BUILD FAILED - bundle not assembled.
    exit /b 1
)
if not exist build\jdbrt.dll (
    echo build\jdbrt.dll missing after build_rt - bundle not assembled.
    exit /b 1
)

echo.
echo === build_full: assemble %OUT% ===
if exist "%OUT%" rmdir /S /Q "%OUT%"
mkdir "%OUT%"
mkdir "%OUT%\doc"
mkdir "%OUT%\tools"

copy /Y build\jdBasic.exe      "%OUT%\" >nul
copy /Y build\jdbrt.lib        "%OUT%\" >nul
copy /Y build\jdb_runtime.obj  "%OUT%\" >nul
copy /Y build\jdbasic_default.ttf "%OUT%\" >nul

REM Every DLL the build placed next to the EXE: SDL3, OpenSSL, LLVM-C, the
REM runtime, ONNX, llama.cpp with its CPU and CUDA back ends, CPython.
REM SDL2.dll and screencap.dll are leftovers of older builds.
for %%D in (build\*.dll) do (
    if /I not "%%~nxD"=="SDL2.dll" if /I not "%%~nxD"=="SDL2_mixer.dll" if /I not "%%~nxD"=="screencap.dll" (
        copy /Y "%%D" "%OUT%\" >nul
    )
)

copy /Y LICENSE.txt              "%OUT%\"     >nul
copy /Y THIRD_PARTY_LICENSES.txt "%OUT%\"     >nul
copy /Y doc\MCP.md               "%OUT%\doc\" >nul
copy /Y doc\languages.md         "%OUT%\doc\" >nul
copy /Y doc\signatures.md        "%OUT%\doc\" >nul
copy /Y help.txt                 "%OUT%\"     >nul

REM The hooks other repositories wire in, with the jdBasic tools they call.
copy /Y tools\hooks\check_images.ps1  "%OUT%\tools\" >nul
copy /Y tools\hooks\commit_files.ps1  "%OUT%\tools\" >nul
copy /Y tools\hooks\image_privacy.jdb "%OUT%\tools\" >nul
copy /Y tools\hooks\session_end.jdb   "%OUT%\tools\" >nul
copy /Y tools\install_bin.jdb         "%OUT%\tools\" >nul

call .\release_content.bat "%OUT%" GFX
if errorlevel 1 (
    echo CONTENT COPY FAILED - bundle not assembled.
    exit /b 1
)

copy /Y "%SystemRoot%\System32\vcruntime140.dll"   "%OUT%\" >nul
copy /Y "%SystemRoot%\System32\vcruntime140_1.dll" "%OUT%\" >nul
copy /Y "%SystemRoot%\System32\msvcp140.dll"       "%OUT%\" >nul

set /p BNUM=<build_number.txt
if "%REL%"=="" set BNUM=0 dev
set BDATE=%date:~6,4%/%date:~3,2%/%date:~0,2%
for /f %%C in ('git rev-parse --short HEAD') do set COMMIT=%%C
> "%OUT%\BUILD_INFO.txt" echo jdBasic Full Pack - Build !BNUM! - !BDATE!
>>"%OUT%\BUILD_INFO.txt" echo.
>>"%OUT%\BUILD_INFO.txt" echo Commit: !COMMIT!
>>"%OUT%\BUILD_INFO.txt" echo Build flags: %FLAGS% %REL%

echo.
echo === build_full: zip ===
if exist "%ZIP%" del "%ZIP%"
powershell -NoProfile -Command "Compress-Archive -Path '%OUT%\*' -DestinationPath '%ZIP%' -Force"
if errorlevel 1 (
    echo ZIP FAILED.
    exit /b 1
)
powershell -NoProfile -Command "(Get-FileHash -Algorithm SHA256 '%ZIP%').Hash" > "%ZIP%.sha256"
set /p HASH=<"%ZIP%.sha256"

echo.
echo BUNDLE OK
echo   %ZIP%
echo   sha256  %HASH%
popd
endlocal

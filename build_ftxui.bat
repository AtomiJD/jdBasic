@echo off
setlocal enabledelayedexpansion

REM build_ftxui.bat: one-time build of the FTXUI static library.
REM Compiles all production .cpp under libs\ftxui\src (excluding _test /
REM _fuzzer / _benchmark) and packs them into libs\ftxui\build\ftxui.lib.
REM Re-run manually after pulling FTXUI updates; build.bat picks the lib up
REM via /D FTXUI and skips the rebuild on subsequent runs.

pushd "%~dp0"

set MSVC=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.44.35207
set SDK=C:\Program Files (x86)\Windows Kits\10
set SDKV=10.0.26100.0
set "CC=%MSVC%\bin\Hostx64\x64\cl.exe"
set "LIB_TOOL=%MSVC%\bin\Hostx64\x64\lib.exe"

if not exist libs\ftxui (
    echo libs\ftxui not found. Clone first:
    echo   cd libs ^&^& git clone --depth 1 --branch v6.1.9 https://github.com/ArthurSonzogni/FTXUI.git ftxui
    exit /b 1
)
if not exist libs\ftxui\build mkdir libs\ftxui\build

REM Per-subdir compile: dom/color.cpp and screen/color.cpp share a
REM basename (same with util.cpp). /Fo:dir\ uses basename only and would
REM let one .obj clobber the other. Compile each subdir into its own
REM output folder so both objects survive into the lib.
REM
REM /Ilibs\ftxui\src is required because FTXUI keeps private headers
REM (node_decorator.hpp, box_helper.hpp, terminal_input_parser.hpp, ...)
REM next to the .cpp files. CMake adds it automatically; we do it here.
REM /DUNICODE /D_UNICODE: FTXUI's screen_interactive.cpp errors out
REM otherwise, needs WindowsW APIs to talk to the modern Win console.
set CL_FLAGS=/nologo /std:c++17 /O2 /EHsc /MP32 /c /MD /DUNICODE /D_UNICODE
set CL_INCS=/I"%MSVC%\include" /I"%SDK%\Include\%SDKV%\ucrt" /I"%SDK%\Include\%SDKV%\um" /I"%SDK%\Include\%SDKV%\shared" /Ilibs\ftxui\include /Ilibs\ftxui\src

REM util/ subdir contains only ref_test.cpp (test file), skip it.
for %%S in (component dom screen) do (
    echo === build_ftxui: %%S ===
    if not exist libs\ftxui\build\%%S mkdir libs\ftxui\build\%%S
    powershell -NoProfile -Command "Get-ChildItem -Path 'libs\ftxui\src\ftxui\%%S' -Filter '*.cpp' | Where-Object { $_.Name -notmatch '_test\.|_fuzzer\.|_benchmark\.' } | ForEach-Object { '\"' + $_.FullName + '\"' } | Out-File -Encoding ASCII libs\ftxui\build\%%S\sources.rsp"
    "%CC%" %CL_FLAGS% %CL_INCS% /Fo:libs\ftxui\build\%%S\ @libs\ftxui\build\%%S\sources.rsp
    if errorlevel 1 (
        echo COMPILE FAILED in %%S.
        exit /b 1
    )
)

echo === build_ftxui: packing static lib ===
"%LIB_TOOL%" /nologo /OUT:libs\ftxui\build\ftxui.lib ^
  libs\ftxui\build\component\*.obj ^
  libs\ftxui\build\dom\*.obj ^
  libs\ftxui\build\screen\*.obj

if errorlevel 1 (
    echo LIB PACK FAILED.
    exit /b 1
)

for %%I in (libs\ftxui\build\ftxui.lib) do set LIB_SIZE=%%~zI
echo.
echo BUILD OK: libs\ftxui\build\ftxui.lib  ^(%LIB_SIZE% bytes^)
popd
endlocal

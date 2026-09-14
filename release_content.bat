@echo off
setlocal enabledelayedexpansion

REM release_content.bat - copies the module library and the curated example
REM programs into a release pack.
REM
REM   call release_content.bat <pack_dir>        console and HTTP examples
REM   call release_content.bat <pack_dir> GFX    plus the graphics and ImGui examples
REM
REM IMPORT resolves lib\ next to jdBasic.exe, so the modules need no setup.
REM lib\ and examples\ are replaced on every run.

pushd "%~dp0"

set PACK=%~1
if "%PACK%"=="" goto usage

set EXAMPLES=jdb\demos\apl\array_idioms.jdb jdb\demos\apl\prime_sieve.jdb jdb\demos\apl\pipe_v1.jdb
set EXAMPLES=!EXAMPLES! jdb\demos\data\agg.jdb jdb\tutorials\func_factory.jdb jdb\tutorials\destructure.jdb
set EXAMPLES=!EXAMPLES! jdb\tutorials\new_syntax_tour.jdb jdb\demos\workflow\re_basics.jdb jdb\demos\tensor\nl_part4.jdb
set EXAMPLES=!EXAMPLES! jdb\demos\games\tetris_game.jdb jdb\demos\jdlibs\sales_dashboard.jdb jdb\demos\jdlibs\parsec_demo.jdb
set EXAMPLES=!EXAMPLES! jdb\demos\jdlibs\pdfgen_demo.jdb jdb\demos\jdlibs\svg_demo.jdb jdb\demos\jdlibs\testkit_demo.jdb
set EXAMPLES=!EXAMPLES! jdb\demos\jdlibs\jdweb_demo.jdb
if /I "%~2"=="GFX" set EXAMPLES=!EXAMPLES! jdb\demos\graphics\mandel_zoom.jdb jdb\demos\graphics\universe.jdb jdb\demos\games\raytracer.jdb jdb\demos\gui\gui_full.jdb

set MISSING=0
for %%F in (!EXAMPLES!) do if not exist "%%F" (
    echo release_content: missing %%F
    set MISSING=1
)
if not exist jdb\demos\jdlibs\grammars\arith.peg set MISSING=1
if not exist lib\README.md set MISSING=1
if "!MISSING!"=="1" goto failed

if exist "%PACK%\lib" rmdir /S /Q "%PACK%\lib"
if exist "%PACK%\examples" rmdir /S /Q "%PACK%\examples"
mkdir "%PACK%\lib"
mkdir "%PACK%\examples\grammars"

copy /Y lib\*.jdb "%PACK%\lib\" >nul
if errorlevel 1 goto failed
copy /Y lib\*.md "%PACK%\lib\" >nul
if errorlevel 1 goto failed
for %%F in (!EXAMPLES!) do (
    copy /Y "%%F" "%PACK%\examples\" >nul
    if errorlevel 1 goto failed
)
copy /Y jdb\demos\jdlibs\grammars\*.peg "%PACK%\examples\grammars\" >nul
if errorlevel 1 goto failed

set README=%PACK%\examples\README.txt
> "%README%" echo jdBasic example programs
>>"%README%" echo.
>>"%README%" echo Run them from this folder, so the files they read and write stay here:
>>"%README%" echo     cd examples
>>"%README%" echo     ..\jdBasic.exe prime_sieve.jdb
>>"%README%" echo.
>>"%README%" echo The jdlibs demos IMPORT modules from ..\lib, which jdBasic finds next to the EXE.
>>"%README%" echo.
>>"%README%" echo Language
>>"%README%" echo   array_idioms.jdb      whole-array operations instead of loops
>>"%README%" echo   prime_sieve.jdb       set-based prime sieve
>>"%README%" echo   pipe_v1.jdb           the pipe operator
>>"%README%" echo   agg.jdb               AGG group-by with a function reference
>>"%README%" echo   func_factory.jdb      closures
>>"%README%" echo   destructure.jdb       destructuring assignment
>>"%README%" echo   new_syntax_tour.jdb   optional chaining, defaults, string interpolation
>>"%README%" echo   re_basics.jdb         regular expressions
>>"%README%" echo   nl_part4.jdb          a trainable neural network learns XOR
>>"%README%" echo   tetris_game.jdb       Tetris in the console
>>"%README%" echo.
>>"%README%" echo Modules from lib
>>"%README%" echo   sales_dashboard.jdb   quarterly report from CSV with DF, DT and CONSOLE
>>"%README%" echo   parsec_demo.jdb       parser combinators, grammars in grammars\
>>"%README%" echo   pdfgen_demo.jdb       an invoice as PDF
>>"%README%" echo   svg_demo.jdb          SVG charts in an HTML report
>>"%README%" echo   testkit_demo.jdb      unit tests with TESTKIT
>>"%README%" echo   jdweb_demo.jdb        a web app with JDWEB that calls itself
if /I not "%~2"=="GFX" goto readme_done
>>"%README%" echo.
>>"%README%" echo Graphics
>>"%README%" echo   mandel_zoom.jdb       Mandelbrot set, click to zoom, ESC to leave
>>"%README%" echo   universe.jdb          vectorized Bubble Universe
>>"%README%" echo   raytracer.jdb         vectorized raytracer with shadows
>>"%README%" echo   gui_full.jdb          ImGui feature tour
:readme_done

echo release_content: lib and examples copied to %PACK%
popd
endlocal
exit /b 0

:usage
echo usage: release_content.bat pack_dir [GFX]
popd
endlocal
exit /b 1

:failed
echo release_content: FAILED - lib or examples incomplete in %PACK%
popd
endlocal
exit /b 1

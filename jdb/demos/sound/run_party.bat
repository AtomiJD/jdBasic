@echo off
rem run_party.bat - plays the party tracks one after another, fullscreen,
rem and starts over after the last one.
rem
rem   Esc  skips to the next track
rem   Q    ends the party
rem   F    toggles fullscreen

setlocal
cd /d "%~dp0"
set "JDB=%~dp0..\..\..\build\jdBasic.exe"
if not exist "%JDB%" (
    echo jdBasic.exe not found at %JDB%
    exit /b 1
)

:party
for %%S in (synthwave_highway.jdb disco_glitter.jdb house_rooftop.jdb acid_copper.jdb trance_live.jdb dnb_nightbus.jdb) do (
    echo.
    echo === %%S ===
    "%JDB%" %%S
    if errorlevel 3 goto done
)
goto party

:done
echo Party over.
endlocal

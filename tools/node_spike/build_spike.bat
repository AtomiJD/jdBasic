@echo off
setlocal
if not defined MSVC set MSVC=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.44.35207
if not defined SDK set SDK=C:\Program Files (x86)\Windows Kits\10
if not defined SDKV set SDKV=10.0.26100.0
cd /d "%~dp0"
"%MSVC%\bin\Hostx64\x64\cl.exe" /nologo /std:c++17 /O2 /EHsc /MD ^
  /I"%MSVC%\include" /I"%SDK%\Include\%SDKV%\ucrt" /I"%SDK%\Include\%SDKV%\um" /I"%SDK%\Include\%SDKV%\shared" ^
  node_spike.cpp /Fe:node_spike.exe ^
  /link /LIBPATH:"%MSVC%\lib\x64" /LIBPATH:"%SDK%\Lib\%SDKV%\ucrt\x64" /LIBPATH:"%SDK%\Lib\%SDKV%\um\x64" ws2_32.lib
if errorlevel 1 exit /b 1
del node_spike.obj 2>nul
echo BUILD OK

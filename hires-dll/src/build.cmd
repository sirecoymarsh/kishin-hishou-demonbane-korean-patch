@echo off
rem Build khd4k.dll (32-bit) with the VS 2022 Build Tools.   usage: build.cmd  ->  ..\out\khd4k.dll
setlocal
set HERE=%~dp0
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars32.bat" >nul || exit /b 1
if not exist "%HERE%..\out" mkdir "%HERE%..\out"
if not exist "%HERE%..\obj" mkdir "%HERE%..\obj"
cl /nologo /utf-8 /W4 /WX /O2 /MT /LD /EHsc /Fo"%HERE%..\obj\\" "%HERE%khd4k.cpp" "%HERE%trace.cpp" "%HERE%display.cpp" "%HERE%probe.cpp" "%HERE%uhd.cpp" "%HERE%battle.cpp" "%HERE%movie.cpp" "%HERE%hires.cpp" /Fe"%HERE%..\out\khd4k.dll" /link /NOLOGO /MAP:"%HERE%..\obj\khd4k.map" user32.lib gdi32.lib kernel32.lib || exit /b 1
echo built

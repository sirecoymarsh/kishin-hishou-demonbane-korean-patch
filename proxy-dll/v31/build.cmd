@echo off
setlocal
set HERE=%~dp0
set TEMP=%HERE%tmp
set TMP=%HERE%tmp
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars32.bat" >nul || exit /b 1
if not exist "%HERE%out" mkdir "%HERE%out"
if not exist "%HERE%obj" mkdir "%HERE%obj"
cl /nologo /utf-8 /W4 /WX /O2 /MT /LD /EHsc /DUNICODE /D_UNICODE /Fo"%HERE%obj\\" "%HERE%src\dinput8_proxy.cpp" /Fe"%HERE%out\dinput8.dll" /link /DEF:"%HERE%src\dinput8.def" /NOLOGO /IMPLIB:"%HERE%obj\dinput8_proxy.lib" user32.lib kernel32.lib ole32.lib advapi32.lib gdi32.lib
exit /b %errorlevel%


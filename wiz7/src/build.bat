@echo off
rem Build wing32.dll (x86) for the Wizardry 7 Japanese mod
setlocal
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars32.bat" >nul || exit /b 1
cd /d "%~dp0"
if not exist ..\build mkdir ..\build
cl /nologo /O2 /W3 /MT /utf-8 /D_CRT_SECURE_NO_WARNINGS /LD wing32_proxy.c ^
   /Fe..\build\wing32.dll /Fo..\build\ ^
   /link /DEF:wing32.def user32.lib gdi32.lib kernel32.lib || exit /b 1
echo build ok: ..\build\wing32.dll

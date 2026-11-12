@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" x86 >nul || exit /b 1
cd /d "%~dp0"
if not exist out mkdir out
cl /nologo /O2 /MT /W3 /LD /D_CRT_SECURE_NO_WARNINGS jphook.c /Fo:out\ /Fe:out\ddraw.dll /link /DEF:ddraw.def /NODEFAULTLIB:libcmtd user32.lib gdi32.lib kernel32.lib || exit /b 1
echo built out\ddraw.dll

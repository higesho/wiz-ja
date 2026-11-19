@echo off
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d "%~dp0"
if not exist out mkdir out
cl /nologo /O2 /W3 /D_CRT_SECURE_NO_WARNINGS /utf-8 /DUNICODE /D_UNICODE wiz7map.c /Fe:out\wiz7map.exe /Fo:out\ /link /SUBSYSTEM:WINDOWS user32.lib gdi32.lib kernel32.lib

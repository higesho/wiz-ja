@echo off
rem _cc7.bat       release: out\wiz7map.exe
rem _cc7.battest   test (WIZTOOL_TEST: diagnostics command line, log): out\test\wiz7map.exe (do not ship)
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d "%~dp0"
set O=out
set DEF=
if "%1"=="test" (
  set O=out\test
  set DEF=/DWIZTOOL_TEST
)
if not exist %O% mkdir %O%
rc /nologo /fo %O%\wiz7map.res wiz7map.rc || exit /b 1
cl /nologo /O2 /W3 /D_CRT_SECURE_NO_WARNINGS /utf-8 /DUNICODE /D_UNICODE %DEF% wiz7map.c %O%\wiz7map.res /Fe:%O%\wiz7map.exe /Fo:%O%\ /link /SUBSYSTEM:WINDOWS user32.lib gdi32.lib kernel32.lib

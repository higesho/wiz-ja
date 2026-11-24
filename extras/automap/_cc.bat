@echo off
rem _cc.bat        release: out\wiz6map.exe
rem _cc.bat test   test (WIZTOOL_TEST: diagnostics command line, log): out\test\wiz6map.exe (do not ship)
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d "%~dp0"
set O=out
set DEF=
if "%1"=="test" (
  set O=out\test
  set DEF=/DWIZTOOL_TEST
)
if not exist %O% mkdir %O%
rc /nologo /fo %O%\wiz6map.res wiz6map.rc || exit /b 1
cl /nologo /O2 /W3 /D_CRT_SECURE_NO_WARNINGS /utf-8 /DUNICODE /D_UNICODE %DEF% wiz6map.c %O%\wiz6map.res /Fe:%O%\wiz6map.exe /Fo:%O%\ /link /SUBSYSTEM:WINDOWS user32.lib gdi32.lib kernel32.lib

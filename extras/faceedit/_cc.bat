@echo off
rem _cc.bat        release: out\Wiz6Face.exe
rem _cc.bat test   test (WIZTOOL_TEST: diagnostics command line, log): out\test\Wiz6Face.exe (do not ship)
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d "%~dp0"
set O=out
set DEF=
if "%1"=="test" (
  set O=out\test
  set DEF=/DWIZTOOL_TEST
)
if not exist %O% mkdir %O%
rc /nologo /fo %O%\wiz6face.res wiz6face.rc || exit /b 1
cl /nologo /O2 /W3 /D_CRT_SECURE_NO_WARNINGS /utf-8 /DUNICODE /D_UNICODE %DEF% wiz6face.c %O%\wiz6face.res /Fe:%O%\Wiz6Face.exe /Fo:%O%\ /link /SUBSYSTEM:WINDOWS user32.lib gdi32.lib kernel32.lib comdlg32.lib gdiplus.lib

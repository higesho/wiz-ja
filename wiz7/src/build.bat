@echo off
rem Build wing32.dll (x86) for the Wizardry 7 Japanese mod
rem   build.bat       release: ..\build\wing32.dll
rem   build.bat test  test (WIZ7JP_TEST, log/F12 shot/crash log): ..\build\test\wing32.dll (do not ship)
setlocal
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars32.bat" >nul || exit /b 1
cd /d "%~dp0"
set O=..\build
set DEF=
if "%1"=="test" (
  set O=..\build\test
  set DEF=/DWIZ7JP_TEST
)
if not exist %O% mkdir %O%
rc /nologo /fo %O%\wing32.res wing32.rc || exit /b 1
cl /nologo /O2 /W3 /MT /utf-8 /D_CRT_SECURE_NO_WARNINGS %DEF% /LD wing32_proxy.c %O%\wing32.res ^
   /Fe%O%\wing32.dll /Fo%O%\ ^
   /link /DEF:wing32.def user32.lib gdi32.lib kernel32.lib || exit /b 1
echo build ok: %O%\wing32.dll

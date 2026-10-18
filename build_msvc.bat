@echo off
chcp 65001 >nul
rem 用法：在开始菜单打开 "x64 Native Tools Command Prompt for VS"，cd 到本目录后运行
where cl >nul 2>nul || (echo [错误] 请在 "x64 Native Tools Command Prompt for VS" 中运行本脚本 & pause & exit /b 1)
if not exist build mkdir build
rc /nologo /fo build\app.res res\app.rc || goto :fail
cl /nologo /utf-8 /O2 /MT /EHsc /std:c++17 /W3 /DUNICODE /D_UNICODE src\main.cpp build\app.res ^
   /Fo:build\ /Fe:build\LaoChenShot.exe /link /SUBSYSTEM:WINDOWS /MANIFEST:NO || goto :fail
echo.
echo 编译成功：build\LaoChenShot.exe
pause
exit /b 0
:fail
echo 编译失败
pause
exit /b 1

@echo off
chcp 65001 >nul
rem 用法：已安装 MinGW-w64（如 MSYS2 / w64devkit），g++ 与 windres 在 PATH 中
if not exist build mkdir build
windres res\app.rc -O coff -o build\app.res.o || goto :fail
g++ -std=c++17 -O2 -municode -mwindows -static -static-libgcc -static-libstdc++ -s ^
    src\main.cpp build\app.res.o -o build\LaoChenShot.exe ^
    -lgdiplus -lcomctl32 -lshell32 -lole32 -luuid -lcomdlg32 -ldwmapi -limm32 -ladvapi32 -lgdi32 -luser32 || goto :fail
echo 编译成功：build\LaoChenShot.exe
pause
exit /b 0
:fail
echo 编译失败
pause
exit /b 1

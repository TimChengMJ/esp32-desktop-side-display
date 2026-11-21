@echo off
rem 重新构建并运行模拟器（改了 gui.cpp / 字体后运行这个）
set PATH=C:\msys64\mingw64\bin;%PATH%
cd /d "%~dp0"
cmake --build build || (echo BUILD FAILED & pause & exit /b 1)
cd build
esp32_screen_sim.exe

@echo off
rem 运行 LVGL 模拟器（实时窗口，鼠标=触摸）
rem 前提：已用 CMake 构建过 build\esp32_screen_sim.exe
set PATH=C:\msys64\mingw64\bin;%PATH%
cd /d "%~dp0build"
start "" esp32_screen_sim.exe

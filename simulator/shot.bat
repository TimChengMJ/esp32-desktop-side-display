@echo off
rem 截图模式：渲染一帧存 shot.png（用于我/AI 看界面，不弹窗口）
set PATH=C:\msys64\mingw64\bin;%PATH%
cd /d "%~dp0"
cmake --build build || exit /b 1
cd build
esp32_screen_sim.exe --shot
python -c "from PIL import Image; Image.open('shot.bmp').save('shot.png')" 2>nul
echo shot.png ready

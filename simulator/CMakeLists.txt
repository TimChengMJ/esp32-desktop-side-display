cmake_minimum_required(VERSION 3.16)
project(esp32_screen_sim C CXX)

set(CMAKE_CXX_STANDARD 17)
set(LVGL_DIR "C:/Users/TimCheng/Documents/Arduino/libraries/lvgl-8.4.0")   # 改成你的 LVGL 8.4.0 路径
set(SKETCH_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../firmware")

find_package(SDL2 REQUIRED)

# LVGL 源码
file(GLOB_RECURSE LVGL_SRC "${LVGL_DIR}/src/*.c")
add_library(lvgl STATIC ${LVGL_SRC})
target_include_directories(lvgl PUBLIC "${LVGL_DIR}" "${LVGL_DIR}/src")
target_compile_definitions(lvgl PUBLIC LV_CONF_INCLUDE_SIMPLE)
# 优先用 simulator 自己的 lv_conf.h（PC 版，修正了 IRAM_ATTR）
target_include_directories(lvgl BEFORE PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

# 模拟器主程序 + 复用 sketch 的 gui.cpp + 字体
add_executable(esp32_screen_sim
    main.cpp
    sim_stub.cpp
    "E:/07_Workspaces/AI/AI_Project/ESP32_SCREEN/ESP32_SCREEN/gui.cpp"
    "E:/07_Workspaces/AI/AI_Project/ESP32_SCREEN/ESP32_SCREEN/FontCN.c"
    "E:/07_Workspaces/AI/AI_Project/ESP32_SCREEN/ESP32_SCREEN/FontNum_40.c"
    "E:/07_Workspaces/AI/AI_Project/ESP32_SCREEN/ESP32_SCREEN/FontNum_30.c"
    "E:/07_Workspaces/AI/AI_Project/ESP32_SCREEN/ESP32_SCREEN/FontNum_16.c"
    "E:/07_Workspaces/AI/AI_Project/ESP32_SCREEN/ESP32_SCREEN/FontNum_64.c"
    "E:/07_Workspaces/AI/AI_Project/ESP32_SCREEN/ESP32_SCREEN/FontClock_64.c"
    "E:/07_Workspaces/AI/AI_Project/ESP32_SCREEN/ESP32_SCREEN/icons.c"
    "E:/07_Workspaces/AI/AI_Project/ESP32_SCREEN/ESP32_SCREEN/FontWeather_24.c"
    "E:/07_Workspaces/AI/AI_Project/ESP32_SCREEN/ESP32_SCREEN/FontWeather_32.c"
)
target_include_directories(esp32_screen_sim PRIVATE "${LVGL_DIR}" "E:/07_Workspaces/AI/AI_Project/ESP32_SCREEN/ESP32_SCREEN")
target_include_directories(esp32_screen_sim BEFORE PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_definitions(esp32_screen_sim PRIVATE LV_CONF_INCLUDE_SIMPLE)
target_link_libraries(esp32_screen_sim PRIVATE lvgl SDL2::SDL2)

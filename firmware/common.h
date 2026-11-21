#ifndef _COMMON_H
#define _COMMON_H

#define MY_LOG_LEVEL_ERR 0
#define MY_LOG_LEVEL_WARN 1
#define MY_LOG_LEVEL_INFO 2

#define MY_LOG_LEVEL MY_LOG_LEVEL_INFO

#define MY_ERR(format, ...)               \
    if (MY_LOG_LEVEL >= MY_LOG_LEVEL_ERR) \
    esp_rom_printf("[E] " format, ##__VA_ARGS__)
#define MY_WARN(format, ...)               \
    if (MY_LOG_LEVEL >= MY_LOG_LEVEL_WARN) \
    esp_rom_printf("[W] " format, ##__VA_ARGS__)
#define MY_INFO(format, ...)               \
    if (MY_LOG_LEVEL >= MY_LOG_LEVEL_INFO) \
    esp_rom_printf("[I] " format, ##__VA_ARGS__)

#include "config.h"
#include "display.h"
#include "pcmon.h"
#include "weather.h"
#include "gui.h"

#endif

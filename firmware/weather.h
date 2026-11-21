#ifndef _WEATHER_H
#define _WEATHER_H

#include <Arduino.h>

// 屏端自取天气（wttr.in），不依赖 PC
struct WeatherData {
  int   temp;          // 温度 °C
  char  desc[24];      // 中文描述，如 "小雨"
  bool  valid;
  uint32_t last_ms;    // 上次更新 millis
};

extern WeatherData g_weather;

// 启动天气后台任务（WiFi 连上后调用）
void weather_begin();

#endif

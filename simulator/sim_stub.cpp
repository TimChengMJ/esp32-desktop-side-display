// 模拟器 stub：提供 Arduino 兼容 + 假数据，让 gui.cpp 在 PC 上跑
#include "lvgl.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <chrono>
#include "common.h"

// ---- Arduino 兼容 ----
SimSerial Serial;
static unsigned long sim_millis() {
  static auto t0 = std::chrono::steady_clock::now();
  return std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - t0).count();
}
uint32_t millis() { return sim_millis(); }
void delay(uint32_t ms) { (void)ms; }

// ---- 全局数据 ----
SystemData g_sysData = {};
WeatherData g_weather = {12, "多云", true, 0};
volatile bool g_ota_updating = false;
volatile int  g_ota_progress = 0;

bool pcmon_is_fresh() { return true; }
void gui_set_screen_ip(const char *ip);  // 由 gui.cpp 提供

// ---- 填充假数据（每帧前调用一次即可）----
void sim_fill_fake() {
  g_sysData.cpu = 37; g_sysData.cpu_temp = 52; g_sysData.cpu_freq = 3850;
  g_sysData.gpu = 21; g_sysData.gpu_temp = 46;
  g_sysData.gpu_mem_used = 2048; g_sysData.gpu_mem_total = 8192; g_sysData.gpu_power = 180;
  g_sysData.mem = 48; g_sysData.mem_used_gb = 21.2f; g_sysData.mem_total_gb = 45.2f;
  g_sysData.disk = 53; g_sysData.disk_used_gb = 251; g_sysData.disk_total_gb = 476;
  g_sysData.disk_read = 120 * 1024; g_sysData.disk_write = 45 * 1024; g_sysData.disk_active = 63;
  g_sysData.net_down = 890; g_sysData.net_up = 33;
  snprintf(g_sysData.host, sizeof(g_sysData.host), "DESKTOP");
  snprintf(g_sysData.time_str, sizeof(g_sysData.time_str), "17:20");
  snprintf(g_sysData.date_str, sizeof(g_sysData.date_str), "11-19");
  g_sysData.year = 2026; g_sysData.month = 11; g_sysData.day = 19; g_sysData.weekday = 2;
  g_sysData.weather_temp = 12;
  snprintf(g_sysData.weather_desc, sizeof(g_sysData.weather_desc), "多云");
  g_sysData.hot_count = 10;
  const char *hot[10] = {"某某重大新闻标题一","某某新闻标题第二","第三条热搜新闻","第四条新闻内容",
    "第五条","第六条新闻","第七条","第八条","第九条","第十条新闻"};
  for (int i = 0; i < 10; i++) snprintf(g_sysData.hot[i], HOTLIST_TITLE_LEN, "%s", hot[i]);
  g_sysData.last_update_ms = millis();
  g_sysData.valid = true;
  // 工时真实数据：今日 08:43 上班，历史 11/18=211 11/19=699
  g_sysData.work_active = true;
  snprintf(g_sysData.work_start, sizeof(g_sysData.work_start), "08:43");
  snprintf(g_sysData.work_end, sizeof(g_sysData.work_end), "--:--");
  g_sysData.work_minutes = 396;
  int hist[14] = {0,0,0,0,0,0,0,0,0,0,0,211,699,396};
  const char *dts[14] = {"11/07","11/08","11/09","11/10","11/11","11/12","11/13","11/14","11/15","11/16","11/17","11/18","11/19","11/20"};
  for (int i = 0; i < 14; i++) { g_sysData.work_hist[i] = hist[i]; snprintf(g_sysData.work_hist_date[i], 6, "%s", dts[i]); }
  g_weather.temp = 10; g_weather.valid = true;
  snprintf(g_weather.desc, sizeof(g_weather.desc), "晴");
}

// 关于页信息（模拟器假值）
void sys_info(int which, char *buf, int len) {
  static uint32_t base = 0;
  uint32_t s = millis() / 1000;
  switch (which) {
    case 0: snprintf(buf, len, "ESP32-S3 240MHz"); break;
    case 1: snprintf(buf, len, "8MB / 8MB"); break;
    case 2: snprintf(buf, len, "v1.0"); break;
    case 3: snprintf(buf, len, "%u KB", 186 - (s % 20)); break;         // 变化演示
    case 4: snprintf(buf, len, "%u天 %u:%02u", 2, (s/3600)%24, (s/60)%60); break;
    case 5: snprintf(buf, len, "-52 dBm"); break;
    default: snprintf(buf, len, "--");
  }
}

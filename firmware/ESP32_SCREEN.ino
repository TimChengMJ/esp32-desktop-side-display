/*****
 * ESP32-S3 桌面信息副屏
 * 屏：晶驰 4寸 480x480 ST7701 (RGB 18bit) + GT911 触摸
 * PC 端每 2s POST /api/pc 推送 CPU/GPU/天气/热搜，屏负责显示。
 *****/

#include "common.h"
#include <WiFi.h>

#define TASK_PRIORITY_LVGL 8

TaskHandle_t TASK_DISP_HANDLE = NULL;

static void wifi_connect() {
  MY_INFO("WiFi: connecting to %s ...\n", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 40) {
    delay(250);
    attempts++;
  }
  if (WiFi.status() == WL_CONNECTED) {
    MY_INFO("WiFi: connected, IP=%s\n", WiFi.localIP().toString().c_str());
    gui_set_screen_ip(WiFi.localIP().toString().c_str());
    weather_begin();
  } else {
    MY_WARN("WiFi: connect failed (will retry in background)\n");
  }
}

void setup() {
  delay(200);
  Serial.begin(115200);
  MY_INFO("System start...\n");

  i2c_init();

  gfx_pre_init();

#ifdef DISPLAY_DIAG
  MY_INFO("DISPLAY_DIAG mode: LVGL test pattern (red + white cross + corners)\n");
#endif

  wifi_connect();
  pcmon_begin();

  MY_INFO("Start LVGL task..\n");
  delay(100);
  xTaskCreatePinnedToCore(task_lvgl, "task_lvgl", 8192, NULL, TASK_PRIORITY_LVGL, &TASK_DISP_HANDLE, 0);

  MY_INFO("Boot finished.\n");
}

void loop() {
  // HTTP 请求处理放在主循环（core 1），不阻塞 LVGL(core 0)
  pcmon_loop();
  if (WiFi.status() != WL_CONNECTED) {
    static uint32_t last_retry = 0;
    if (millis() - last_retry > 5000) {
      last_retry = millis();
      WiFi.reconnect();
    }
  } else {
    // 连上后同步 IP 到屏幕（IP 变化时更新，覆盖重连换 IP 的情况）
    static char last_ip[16] = "";
    String ip = WiFi.localIP().toString();
    if (ip != last_ip) {
      snprintf(last_ip, sizeof(last_ip), "%s", ip.c_str());
      gui_set_screen_ip(last_ip);
    }
    // 首次 wifi_connect 失败、这里重连成功时，启动天气任务（begin 内部防重复）
    static bool weather_started = false;
    if (!weather_started) {
      weather_started = true;
      weather_begin();
    }
  }
  delay(2);
}

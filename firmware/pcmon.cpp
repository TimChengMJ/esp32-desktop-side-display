#include "common.h"
#include <WebServer.h>
#include <ArduinoJson.h>
#include <Update.h>

SystemData g_sysData = {};

static WebServer server(HTTP_SERVER_PORT);

// ── OTA 上传状态 ──
static bool ota_ok = false;
static bool ota_active = false;
volatile bool g_ota_updating = false;
volatile int  g_ota_progress = 0;   // 0-100

// 数据超过该时间未更新则视为失联
#define DATA_STALE_MS 10000

// 按 UTF-8 字符边界拷贝，避免截断半个汉字导致乱码
static void utf8_copy(char *dst, const char *src, size_t dst_size) {
  size_t i = 0;
  while (src[i] && i < dst_size - 1) {
    unsigned char c = (unsigned char)src[i];
    int len = 1;
    if (c >= 0xF0) len = 4;
    else if (c >= 0xE0) len = 3;
    else if (c >= 0xC0) len = 2;
    // 若剩余空间放不下整个字符，停止
    if (i + len > dst_size - 1) break;
    for (int k = 0; k < len; k++) {
      if (!src[i + k]) { len = k; break; }
      dst[i + k] = src[i + k];
    }
    i += len;
  }
  dst[i] = '\0';
}

bool pcmon_is_fresh() {
  return g_sysData.valid && (millis() - g_sysData.last_update_ms < DATA_STALE_MS);
}

// POST /api/pc
static void handle_pc_post() {
  if (!server.hasArg("plain")) {
    server.send(400, "application/json", "{\"err\":\"no body\"}");
    return;
  }

  // 热搜最多 10 条中文，每条约 90 字节，加上天气/时间等，
  // 文档需足够大，否则 deserializeJson 失败导致整包丢弃（CPU/GPU 也不更新）
  DynamicJsonDocument doc(16384);
  DeserializationError err = deserializeJson(doc, server.arg("plain"));
  if (err) {
    MY_WARN("pcmon: JSON parse error: %s (body %u bytes)\n", err.c_str(), server.arg("plain").length());
    server.send(400, "application/json", "{\"err\":\"bad json\"}");
    return;
  }

  // PC 性能
  g_sysData.cpu        = doc["cpu"]        | g_sysData.cpu;
  g_sysData.cpu_temp   = doc["cpu_temp"]   | g_sysData.cpu_temp;
  g_sysData.cpu_freq   = doc["cpu_freq"]   | g_sysData.cpu_freq;
  g_sysData.gpu        = doc["gpu"]        | g_sysData.gpu;
  g_sysData.gpu_temp   = doc["gpu_temp"]   | g_sysData.gpu_temp;
  g_sysData.gpu_mem_used  = doc["gpu_mem_used"]  | g_sysData.gpu_mem_used;
  g_sysData.gpu_mem_total = doc["gpu_mem_total"] | g_sysData.gpu_mem_total;
  g_sysData.gpu_power  = doc["gpu_power"]  | g_sysData.gpu_power;
  g_sysData.mem        = doc["mem"]        | g_sysData.mem;
  g_sysData.mem_used_gb  = doc["mem_used_gb"]  | g_sysData.mem_used_gb;
  g_sysData.mem_total_gb = doc["mem_total_gb"] | g_sysData.mem_total_gb;
  g_sysData.disk       = doc["disk"]       | g_sysData.disk;
  g_sysData.disk_used_gb  = doc["disk_used_gb"]  | g_sysData.disk_used_gb;
  g_sysData.disk_total_gb = doc["disk_total_gb"] | g_sysData.disk_total_gb;
  g_sysData.disk_read     = doc["disk_read"]     | g_sysData.disk_read;
  g_sysData.disk_write    = doc["disk_write"]    | g_sysData.disk_write;
  g_sysData.disk_active   = doc["disk_active"]   | g_sysData.disk_active;
  g_sysData.net_down   = doc["net_down"]   | g_sysData.net_down;
  g_sysData.net_up     = doc["net_up"]     | g_sysData.net_up;

  const char *host = doc["host"] | "";
  strncpy(g_sysData.host, host, sizeof(g_sysData.host) - 1);
  g_sysData.host[sizeof(g_sysData.host) - 1] = '\0';

  // 时间
  const char *tstr = doc["time"] | "";
  strncpy(g_sysData.time_str, tstr, sizeof(g_sysData.time_str) - 1);
  g_sysData.time_str[sizeof(g_sysData.time_str) - 1] = '\0';
  const char *dstr = doc["date"] | "";
  strncpy(g_sysData.date_str, dstr, sizeof(g_sysData.date_str) - 1);
  g_sysData.date_str[sizeof(g_sysData.date_str) - 1] = '\0';
  g_sysData.year    = doc["year"]    | g_sysData.year;
  g_sysData.month   = doc["month"]   | g_sysData.month;
  g_sysData.day     = doc["day"]     | g_sysData.day;
  g_sysData.weekday = doc["weekday"] | g_sysData.weekday;

  // 天气
  if (doc.containsKey("weather")) {
    JsonObject w = doc["weather"];
    g_sysData.weather_temp = w["temp"] | g_sysData.weather_temp;
    const char *desc = w["desc"] | "";
    utf8_copy(g_sysData.weather_desc, desc, WEATHER_DESC_LEN);
    const char *code = w["code"] | "";
    strncpy(g_sysData.weather_code, code, sizeof(g_sysData.weather_code) - 1);
    g_sysData.weather_code[sizeof(g_sysData.weather_code) - 1] = '\0';
  }

  // 热搜
  if (doc.containsKey("hot") && doc["hot"].is<JsonArray>()) {
    JsonArray arr = doc["hot"];
    g_sysData.hot_count = 0;
    for (JsonVariant v : arr) {
      if (g_sysData.hot_count >= HOTLIST_MAX) break;
      const char *t = v.as<const char *>();
      if (!t) continue;
      utf8_copy(g_sysData.hot[g_sysData.hot_count], t, HOTLIST_TITLE_LEN);
      g_sysData.hot[g_sysData.hot_count][HOTLIST_TITLE_LEN - 1] = '\0';
      g_sysData.hot_count++;
    }
  }

  // 工时
  if (doc.containsKey("work")) {
    JsonObject w = doc["work"];
    g_sysData.work_active = w["active"] | false;
    const char *ws = w["start"] | "--:--";
    strncpy(g_sysData.work_start, ws, sizeof(g_sysData.work_start) - 1);
    g_sysData.work_start[sizeof(g_sysData.work_start) - 1] = '\0';
    const char *we = w["end"] | "--:--";
    strncpy(g_sysData.work_end, we, sizeof(g_sysData.work_end) - 1);
    g_sysData.work_end[sizeof(g_sysData.work_end) - 1] = '\0';
    g_sysData.work_minutes = w["minutes"] | 0;
    JsonArray hist = w["hist"];
    int i = 0;
    for (JsonVariant v : hist) { if (i >= WORK_HIST_DAYS) break; g_sysData.work_hist[i++] = v.as<int>(); }
    JsonArray hd = w["hist_date"];
    int j = 0;
    for (JsonVariant v : hd) {
      if (j >= WORK_HIST_DAYS) break;
      const char *d = v.as<const char *>();
      if (d) { strncpy(g_sysData.work_hist_date[j], d, 5); g_sysData.work_hist_date[j][5] = '\0'; }
      j++;
    }
  }

  g_sysData.last_update_ms = millis();
  g_sysData.valid = true;

  MY_INFO("pcmon: cpu=%d gpu=%d mem=%d hot=%u time=%s\n",
          (int)g_sysData.cpu, (int)g_sysData.gpu, (int)g_sysData.mem,
          g_sysData.hot_count, g_sysData.time_str);

  server.send(200, "application/json", "{\"ok\":1}");
}

// GET /api/ping —— 方便 PC 端探测屏是否在线
static void handle_ping() {
  server.send(200, "application/json", "{\"ok\":1,\"fresh\":true}");
}

static void handle_not_found() {
  server.send(404, "application/json", "{\"err\":\"not found\"}");
}

// ── OTA 上传 ──
static void handle_ota_upload() {
  HTTPUpload &up = server.upload();
  if (up.status == UPLOAD_FILE_START) {
    ota_ok = false;
    ota_active = false;
    g_ota_updating = true;   // 让 gui 显示升级遮罩、暂停刷新
    g_ota_progress = 0;
    MY_INFO("OTA: start %s, size=%u\n", up.filename.c_str(), up.totalSize);
    uint32_t max_sketch = ESP.getFreeSketchSpace();
    if (!Update.begin(max_sketch)) {
      MY_WARN("OTA: begin failed, max=%u\n", max_sketch);
      Update.printError(Serial);
      return;
    }
    ota_active = true;
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (ota_active) {
      if (Update.write(up.buf, up.currentSize) != up.currentSize) {
        MY_WARN("OTA: write failed\n");
        Update.printError(Serial);
        ota_active = false;
      } else {
        if (Update.size() > 0)
          g_ota_progress = (int)((uint64_t)Update.progress() * 100 / Update.size());
        // 每块之间短暂让出，缓解 flash 写入持续电流冲击（背光/供电抖）
        delay(3);
      }
    }
  } else if (up.status == UPLOAD_FILE_END) {
    if (ota_active) {
      if (Update.end(true)) {
        ota_ok = true;
        g_ota_progress = 100;
        MY_INFO("OTA: success %u bytes, rebooting...\n", up.totalSize);
      } else {
        Update.printError(Serial);
        MY_WARN("OTA: end failed\n");
      }
      ota_active = false;
    }
  }
}

static void handle_ota_done() {
  if (ota_ok) {
    server.send(200, "application/json", "{\"ok\":1,\"msg\":\"rebooting\"}");
    delay(500);
    ESP.restart();
  } else {
    g_ota_updating = false;  // 失败，恢复正常 UI
    server.send(500, "application/json", "{\"ok\":0,\"err\":\"ota failed\"}");
  }
}

void pcmon_begin() {
  server.on("/api/pc", HTTP_POST, handle_pc_post);
  server.on("/api/ping", HTTP_GET, handle_ping);
  server.on("/api/ota", HTTP_POST, handle_ota_done, handle_ota_upload);
  server.onNotFound(handle_not_found);
  server.begin();
  MY_INFO("pcmon: HTTP server started on port %d\n", HTTP_SERVER_PORT);
}

// 需在 loop 或独立任务中持续调用
void pcmon_loop() {
  server.handleClient();
}

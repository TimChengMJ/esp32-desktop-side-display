#include "common.h"
#include "weather.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

WeatherData g_weather = {0, "--", false, 0};

static TaskHandle_t s_weather_task = NULL;
static const char *CITY = "wuhan";   // 城市拼音（wttr.in 对拼音更稳）

// 英文天气描述 → 中文（wttr.in 返回英文）
static void desc_to_cn(const char *en, char *out, int maxlen) {
  struct { const char *en; const char *cn; } map[] = {
    {"sunny", "晴"}, {"clear", "晴"},
    {"partly cloudy", "多云"}, {"cloudy", "阴"}, {"overcast", "阴"},
    {"mist", "薄雾"}, {"fog", "雾"}, {"haze", "霾"},
    {"patchy rain", "零星小雨"}, {"light rain", "小雨"}, {"moderate rain", "中雨"},
    {"heavy rain", "大雨"}, {"rain", "雨"}, {"drizzle", "毛毛雨"},
    {"patchy snow", "零星小雪"}, {"light snow", "小雪"}, {"snow", "雪"}, {"blizzard", "暴雪"},
    {"thunder", "雷"}, {"sleet", "雨夹雪"},
  };
  // 全小写匹配
  char low[64]; int n = 0;
  for (; en[n] && n < 63; n++) low[n] = (en[n] >= 'A' && en[n] <= 'Z') ? en[n] + 32 : en[n];
  low[n] = 0;
  for (auto &m : map) {
    if (strstr(low, m.en)) { strncpy(out, m.cn, maxlen - 1); out[maxlen - 1] = 0; return; }
  }
  // 没匹配到，原样截断
  strncpy(out, en, maxlen - 1); out[maxlen - 1] = 0;
}

static bool fetch_weather() {
  if (WiFi.status() != WL_CONNECTED) return false;
  HTTPClient http;
  char url[128];
  // 精简格式：%t 温度 %C 天气描述，返回纯文本如 "+9°C|Smoky haze"
  snprintf(url, sizeof(url), "http://wttr.in/%s?format=%%t|%%C&lang=zh", CITY);
  MY_INFO("weather: GET %s\n", url);
  http.begin(url);
  http.setTimeout(8000);
  int code = http.GET();
  if (code != 200) { MY_WARN("weather: HTTP %d\n", code); http.end(); return false; }
  String body = http.getString();
  http.end();
  body.trim();
  MY_INFO("weather: got '%s' (%d bytes)\n", body.c_str(), body.length());
  if (body.length() == 0) return false;

  // 解析 "+9°C|Smoky haze"
  int bar = body.indexOf('|');
  String tempPart = (bar > 0) ? body.substring(0, bar) : body;
  String descPart = (bar > 0) ? body.substring(bar + 1) : "";
  // 提取温度数字
  int temp = 0;
  bool neg = false;
  for (unsigned i = 0; i < tempPart.length(); i++) {
    char ch = tempPart[i];
    if (ch == '-') neg = true;
    else if (ch >= '0' && ch <= '9') temp = temp * 10 + (ch - '0');
  }
  if (neg) temp = -temp;

  char cn[24];
  desc_to_cn(descPart.c_str(), cn, sizeof(cn));

  g_weather.temp = temp;
  strncpy(g_weather.desc, cn, sizeof(g_weather.desc) - 1);
  g_weather.desc[sizeof(g_weather.desc) - 1] = '\0';
  g_weather.valid = true;
  g_weather.last_ms = millis();
  MY_INFO("weather: %dC %s\n", temp, cn);
  return true;
}

static void weather_task(void *pv) {
  // 首次尽快拉；之后每 30 分钟
  for (;;) {
    fetch_weather();
    vTaskDelay(pdMS_TO_TICKS(30 * 60 * 1000));
  }
}

void weather_begin() {
  if (s_weather_task != NULL) return;   // 防重复启动
  xTaskCreatePinnedToCore(weather_task, "weather", 8192, NULL, 1, &s_weather_task, 1);
}

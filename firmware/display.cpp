#include "common.h"
#include "gui.h"
#include "TAMC_GT911.h"
#include "scr_st7701_4inch_square.h"
#include "lvgl.h"
#include <Wire.h>
#include <WiFi.h>
#include <esp_system.h>

// 关于页信息（真机）：which 0芯片 1flash/psram 2固件 3空闲堆KB 4运行时长 5wifi信号
void sys_info(int which, char *buf, int len) {
  switch (which) {
    case 0:
      snprintf(buf, len, "ESP32-S3 %dMHz", getCpuFrequencyMhz());
      break;
    case 1:
      snprintf(buf, len, "%dMB / %dMB", ESP.getFlashChipSize() / (1024 * 1024),
               ESP.getPsramSize() / (1024 * 1024));
      break;
    case 2:
      snprintf(buf, len, "v1.0");
      break;
    case 3:
      snprintf(buf, len, "%d KB", ESP.getFreeHeap() / 1024);
      break;
    case 4: {
      uint32_t s = millis() / 1000;
      uint32_t d = s / 86400, h = (s / 3600) % 24, m = (s / 60) % 60, sec = s % 60;
      if (d > 0) snprintf(buf, len, "%d天 %d:%02d:%02d", d, h, m, sec);
      else snprintf(buf, len, "%d:%02d:%02d", h, m, sec);
      break;
    }
    case 5:
      if (WiFi.status() == WL_CONNECTED)
        snprintf(buf, len, "%d dBm", WiFi.RSSI());
      else
        snprintf(buf, len, "未连接");
      break;
    default:
      snprintf(buf, len, "--");
  }
}

// 触摸用的 I2C 总线初始化
void i2c_init() {
  Wire.begin(TOUCH_SDA, TOUCH_SCL, 400000);
}

static TAMC_GT911 tp = TAMC_GT911(TOUCH_SDA, TOUCH_SCL, TOUCH_INT, TOUCH_RST, SCREEN_RES_HOR, SCREEN_RES_VER);
static lv_color_t *disp_draw_buf;
static lv_disp_draw_buf_t draw_buf;
static lv_disp_drv_t disp_drv;
static lv_indev_t *indev_touchpad;

static void flush_callback(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p) {
  uint32_t w = (area->x2 - area->x1 + 1);
  uint32_t h = (area->y2 - area->y1 + 1);
  static int dbg_cnt = 0;
  if (dbg_cnt < 3) {
    Serial.printf("[flush] disp=%dx%d area x1=%d y1=%d x2=%d y2=%d (w=%d h=%d)\n",
                  disp->hor_res, disp->ver_res, area->x1, area->y1, area->x2, area->y2, w, h);
    dbg_cnt++;
  }
  gfx->draw16bitRGBBitmap(area->x1, area->y1, (uint16_t *)&color_p->full, w, h);
  lv_disp_flush_ready(disp);
}

static void scr_lvgl_init() {
  size_t lv_cache_rows = 120;
  disp_draw_buf = (lv_color_t *)heap_caps_malloc(lv_cache_rows * SCREEN_RES_HOR * 2, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  lv_init();
  lv_disp_draw_buf_init(&draw_buf, disp_draw_buf, NULL, SCREEN_RES_HOR * lv_cache_rows);
  lv_disp_drv_init(&disp_drv);
  disp_drv.hor_res = SCREEN_RES_HOR;
  disp_drv.ver_res = SCREEN_RES_VER;
  disp_drv.flush_cb = flush_callback;
  disp_drv.draw_buf = &draw_buf;
  lv_disp_drv_register(&disp_drv);
}

static void scr_pre_init() {
  gfx = new Arduino_RGB_Display(
    480, 480, rgbpanel, 0, true,
    bus, GFX_NOT_DEFINED, st7701_type1A_init_operations, sizeof(st7701_type1A_init_operations));

  gfx->begin(12000000L);
  delay(100);
}

static void touchpad_init(void) {
  tp.begin();
  tp.setRotation(ROTATION_INVERTED);
}

static void touchpad_read(lv_indev_drv_t *indev_drv, lv_indev_data_t *data) {
  tp.read();
  if (tp.isTouched) {
    // 调试：打印原始触摸坐标（串口 115200 查看）
    static uint32_t last_dbg = 0;
    if (millis() - last_dbg > 200) {
      Serial.printf("[touch] raw x=%d y=%d touches=%d\n", tp.points[0].x, tp.points[0].y, tp.touches);
      last_dbg = millis();
    }
    data->point.x = tp.points[0].x;
    data->point.y = tp.points[0].y;
    data->state = LV_INDEV_STATE_PR;
  } else {
    data->state = LV_INDEV_STATE_REL;
  }
}

void gfx_pre_init() {
  touchpad_init();
  scr_pre_init();
}

void display_diag_pattern() {
  gfx->fillScreen(0xF800);
  gfx->drawRect(0, 0, 480, 480, 0xFFFF);
  gfx->drawRect(1, 1, 478, 478, 0xFFFF);
  gfx->fillRect(237, 0, 6, 480, 0xFFFF);
  gfx->fillRect(0, 237, 480, 6, 0xFFFF);
}

static void indev_init() {
  static lv_indev_drv_t indev_drv;
  lv_indev_drv_init(&indev_drv);
  indev_drv.type = LV_INDEV_TYPE_POINTER;
  indev_drv.read_cb = touchpad_read;
  indev_touchpad = lv_indev_drv_register(&indev_drv);
}

void task_lvgl(void *pvParameters) {
  scr_lvgl_init();
  indev_init();
  MY_INFO("LVGL Init complete.\n");
  load_ui();
  uint32_t task_delay_ms = 1;
  for (;;) {
    // OTA 升级时也持续渲染（进度条要更新）；花屏是 DMA/总线硬件限制，LVGL 暂停也无济于事
    task_delay_ms = lv_task_handler();
    task_delay_ms = constrain(task_delay_ms, 2, 10);
    vTaskDelay(pdMS_TO_TICKS(task_delay_ms));
  }
}

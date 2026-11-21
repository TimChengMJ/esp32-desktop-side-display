// LVGL SDL 模拟器主程序：480x480 窗口，鼠标=触摸，点左下/右侧模拟翻页
#include "lvgl.h"
#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#define SIM_W 480
#define SIM_H 480

void sim_fill_fake();
void load_ui();
void gui_debug_page(int p);
void gui_debug_menu();

static SDL_Window *win;
static SDL_Renderer *ren;
static SDL_Texture *tex;
static uint32_t *fb;
static lv_disp_draw_buf_t draw_buf;
static lv_color_t buf1[SIM_W * 100];

static int flush_count = 0;
static void disp_flush(lv_disp_drv_t *d, const lv_area_t *a, lv_color_t *c) {
  if (flush_count < 3) { printf("flush #%d area %d,%d-%d,%d fmtfull=%d\n", flush_count++, a->x1,a->y1,a->x2,a->y2, (int)sizeof(lv_color_t)); }
  uint32_t w = a->x2 - a->x1 + 1, h = a->y2 - a->y1 + 1;
  for (uint32_t y = 0; y < h; y++) {
    for (uint32_t x = 0; x < w; x++) {
      lv_color_t px = c[y * w + x];
      // RGB565 -> RGB888 正确扩展
      uint32_t r = (px.ch.red * 255 + 15) / 31;
      uint32_t g = (px.ch.green * 255 + 31) / 63;
      uint32_t b = (px.ch.blue * 255 + 15) / 31;
      fb[(a->y1 + y) * SIM_W + (a->x1 + x)] = 0xFF000000 | (r << 16) | (g << 8) | b;
    }
  }
  lv_disp_flush_ready(d);
}

// 鼠标模拟触摸
static int mx = -1, my = -1;
static bool pressed = false;
static void indev_read(lv_indev_drv_t *drv, lv_indev_data_t *data) {
  data->point.x = mx; data->point.y = my;
  data->state = pressed ? LV_INDEV_STATE_PR : LV_INDEV_STATE_REL;
}

int main(int argc, char **argv) {
  SDL_SetMainReady();
  SDL_Init(SDL_INIT_VIDEO);
  win = SDL_CreateWindow("ESP32 Screen Sim 480x480", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, SIM_W, SIM_H, 0);
  ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
  tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, SIM_W, SIM_H);
  fb = (uint32_t *)malloc(SIM_W * SIM_H * 4);

  lv_init();
  lv_disp_draw_buf_init(&draw_buf, buf1, NULL, SIM_W * 100);
  static lv_disp_drv_t disp_drv;
  lv_disp_drv_init(&disp_drv);
  disp_drv.hor_res = SIM_W; disp_drv.ver_res = SIM_H;
  disp_drv.flush_cb = disp_flush; disp_drv.draw_buf = &draw_buf;
  lv_disp_drv_register(&disp_drv);

  static lv_indev_drv_t indev_drv;
  lv_indev_drv_init(&indev_drv);
  indev_drv.type = LV_INDEV_TYPE_POINTER;
  indev_drv.read_cb = indev_read;
  lv_indev_drv_register(&indev_drv);

  sim_fill_fake();
  load_ui();

  // --shot [page]：离屏渲染几帧后存 BMP（用于开发验证界面）
  bool shot = (argc > 1 && strcmp(argv[1], "--shot") == 0);
  if (shot) {
    if (argc > 2) gui_debug_page(atoi(argv[2]));
    if (argc > 3 && strcmp(argv[3], "menu") == 0) gui_debug_menu();
    for (int i = 0; i < 200; i++) { lv_tick_inc(10); sim_fill_fake(); lv_timer_handler(); }
    lv_obj_t *act = lv_scr_act();
    printf("active screen children=%u\n", lv_obj_get_child_cnt(act));
    for (uint32_t k = 0; k < lv_obj_get_child_cnt(act); k++) {
      lv_obj_t *ch = lv_obj_get_child(act, k);
      printf("child%u pos(%d,%d) size(%d,%d) kids=%u\n", k, lv_obj_get_x(ch), lv_obj_get_y(ch), lv_obj_get_width(ch), lv_obj_get_height(ch), lv_obj_get_child_cnt(ch));
      for (uint32_t j = 0; j < lv_obj_get_child_cnt(ch); j++) {
        lv_obj_t *l = lv_obj_get_child(ch, j);
        if (lv_obj_check_type(l, &lv_label_class)) {
          printf("    L pos(%d,%d) size(%d,%d) '%s'\n", lv_obj_get_x(l), lv_obj_get_y(l), lv_obj_get_width(l), lv_obj_get_height(l), lv_label_get_text(l));
        }
      }
    }
    // 存为 BMP (BGRA bottom-up)
    FILE *f = fopen("shot.bmp", "wb");
    if (f) {
      int row = SIM_W * 4, size = row * SIM_H, fileSize = 54 + size;
      unsigned char hdr[54] = {0};
      hdr[0]='B'; hdr[1]='M';
      *(int*)&hdr[2]=fileSize; *(int*)&hdr[10]=54; *(int*)&hdr[14]=40;
      *(int*)&hdr[18]=SIM_W; *(int*)&hdr[22]=SIM_H; hdr[26]=1; hdr[28]=32;
      *(int*)&hdr[34]=size;
      fwrite(hdr,1,54,f);
      for (int y = SIM_H-1; y >= 0; y--) fwrite(&fb[y*SIM_W],1,row,f);
      fclose(f);
      printf("saved shot.bmp\n");
    }
    return 0;
  }

  bool quit = false;
  uint32_t last_tick = SDL_GetTicks();
  while (!quit) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
      if (e.type == SDL_QUIT) quit = true;
      else if (e.type == SDL_MOUSEMOTION) { mx = e.motion.x; my = e.motion.y; }
      else if (e.type == SDL_MOUSEBUTTONDOWN) {
        pressed = true; mx = e.button.x; my = e.button.y;
        // 底部/顶部点击交给人机接口，控件自己处理
      } else if (e.type == SDL_MOUSEBUTTONUP) { pressed = false; }
      else if (e.type == SDL_KEYDOWN) {
        // 1/2/3 切换页面便于调试
      }
    }
    sim_fill_fake();
    lv_timer_handler();
    SDL_UpdateTexture(tex, NULL, fb, SIM_W * 4);
    SDL_RenderClear(ren);
    SDL_RenderCopy(ren, tex, NULL, NULL);
    SDL_RenderPresent(ren);
    uint32_t now = SDL_GetTicks();
    uint32_t diff = now - last_tick;
    lv_tick_inc(diff);
    last_tick = now;
    SDL_Delay(5);
  }
  SDL_DestroyTexture(tex); SDL_DestroyRenderer(ren); SDL_DestroyWindow(win); SDL_Quit();
  return 0;
}

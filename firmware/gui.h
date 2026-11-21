#ifndef _GUI_H
#define _GUI_H

#include <lvgl.h>

void load_ui();

// 设置屏自身 IP 用于屏幕显示（.ino 连上 WiFi 后调用）
void gui_set_screen_ip(const char *ip);

// 调试：切换到指定页（0主页 1新闻 2详情），模拟器截图用
void gui_debug_page(int p);
void gui_debug_menu();

#endif

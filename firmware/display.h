#ifndef _DISPLAY_H
#define _DISPLAY_H

#include <Arduino.h>
#include <lvgl.h>

void i2c_init();
void gfx_pre_init();
void display_diag_pattern();

void task_lvgl(void *pvParameters);

#endif

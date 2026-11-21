#ifndef __CONFIG_H
#define __CONFIG_H

// ============ 屏幕：晶驰 4寸方屏 (ST7701, 480x480, RGB 18bit) ============
#define LCD_RGB_VER 1  // 1 = 淘晶驰4寸屏（触摸排线和屏幕排线分开）

#define TFT_RST (-1)
#define TFT_SPI_MOSI 12
#define TFT_SPI_SCK 13
#define TFT_SPI_CS 45

#define TFT_RGB_DE 47
#define TFT_RGB_VSYNC 21
#define TFT_RGB_HSYNC 14
#define TFT_RGB_PCLK 48

// 按顺序是 DB1-DB17，没有 DB12
#define TFT_RGB_B0 13
#define TFT_RGB_B1 12
#define TFT_RGB_B2 11
#define TFT_RGB_B3 10
#define TFT_RGB_B4 9

#define TFT_RGB_G0 46
#define TFT_RGB_G1 3
#define TFT_RGB_G2 20
#define TFT_RGB_G3 19
#define TFT_RGB_G4 8
#define TFT_RGB_G5 18

#define TFT_RGB_R0 17
#define TFT_RGB_R1 16
#define TFT_RGB_R2 15
#define TFT_RGB_R3 7
#define TFT_RGB_R4 6

// ============ 触摸 (GT911, I2C) ============
#define TOUCH_SDA 5
#define TOUCH_SCL 4
#define TOUCH_INT -1
#define TOUCH_RST -1

// ============ 屏幕分辨率 ============
#define SCREEN_RES_HOR 480
#define SCREEN_RES_VER 480
#define SCREEN_CENTER_X 240
#define SCREEN_CENTER_Y 240

// ============ Wi-Fi（屏连接的局域网） ============
#define WIFI_SSID "YOUR_WIFI_SSID"
#define WIFI_PASS "YOUR_WIFI_PASSWORD"

// ============ PC 数据推送服务端口 ============
#define HTTP_SERVER_PORT 80

#endif

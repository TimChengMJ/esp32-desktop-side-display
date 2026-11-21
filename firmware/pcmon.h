#ifndef _PC_MON_H_
#define _PC_MON_H_

#include <Arduino.h>

// 热搜条最多显示的条数
#define HOTLIST_MAX 10
#define HOTLIST_TITLE_LEN 64

// 天气描述最大长度
#define WEATHER_DESC_LEN 24

// PC 端每 2s 推送过来的状态
struct SystemData {
  // PC 性能
  float cpu;         // CPU 占用 %
  float cpu_temp;    // CPU 温度 °C
  int   cpu_freq;    // CPU 频率 MHz
  float gpu;         // GPU 占用 %
  float gpu_temp;    // GPU 温度 °C
  int   gpu_mem_used;   // GPU 显存已用 MB
  int   gpu_mem_total;  // GPU 显存总 MB
  int   gpu_power;      // GPU 功耗 W
  float mem;         // 内存占用 %
  float mem_used_gb; // 内存已用 GB
  float mem_total_gb;// 内存总 GB
  float disk;        // 磁盘占用 %
  int   disk_used_gb;// 磁盘已用 GB
  int   disk_total_gb; // 磁盘总 GB
  float disk_read;   // 磁盘读 KB/s
  float disk_write;  // 磁盘写 KB/s
  int   disk_active; // 磁盘活跃度 %
  float net_down;    // 下行 KB/s
  float net_up;      // 上行 KB/s
  char  host[32];    // 主机名

  // 时间
  char time_str[8];  // "14:30"
  char date_str[8];  // "11-19"
  int  year;         // 2026
  int  month;        // 1-12
  int  day;          // 1-31
  int  weekday;      // 0=周一

  // 天气
  float weather_temp;
  char  weather_desc[WEATHER_DESC_LEN];
  char  weather_code[16];  // sunny/cloudy/rain/...

  // 热搜
  uint8_t hot_count;
  char    hot[HOTLIST_MAX][HOTLIST_TITLE_LEN];

  // 工时（PC 端计算好推送）
  bool  work_active;     // 今天是否已上班（有上班时间）
  char  work_start[6];   // "09:00" 上班时间
  char  work_end[6];     // "18:30" 下班时间（未下班为 "--:--"）
  int   work_minutes;    // 今日工时分钟
  // 14 天历史：每天工时分钟（0=无）
  #define WORK_HIST_DAYS 14
  int   work_hist[WORK_HIST_DAYS];   // [0]=最早, [13]=今天
  char  work_hist_date[WORK_HIST_DAYS][6];  // "11/19"
  // 数据新鲜度（屏端维护，非 PC 提供）
  uint32_t last_update_ms;  // 上次收到 POST 的 millis
  bool     valid;           // 是否收到过有效数据
};

extern SystemData g_sysData;

// OTA 进行中标志（gui 据此显示升级遮罩并暂停刷新）
extern volatile bool g_ota_updating;
extern volatile int  g_ota_progress;   // 0-100

// 启动 HTTP 服务器（在 setup 调用，需 Wi-Fi 已连）
void pcmon_begin();

// 需在 loop/任务中持续调用，处理 HTTP 请求
void pcmon_loop();

// 判断数据是否新鲜（超时则 UI 置灰）
bool pcmon_is_fresh();

#endif

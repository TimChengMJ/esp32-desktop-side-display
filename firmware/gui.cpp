#include "common.h"
#include <lvgl.h>

LV_FONT_DECLARE(FontGB2)        // 中文 + ASCII
LV_FONT_DECLARE(FontNum_40)     // 数值
LV_FONT_DECLARE(FontNum_30)     // 数值+字母(网速用)
LV_FONT_DECLARE(FontNum_16)     // 副值数字(16px 统一数字风格)
LV_FONT_DECLARE(FontNum_64)     // 大时钟数字
LV_FONT_DECLARE(FontWeather_24) // 天气描述 + °C (24px)
LV_FONT_DECLARE(FontWeather_32) // 天气温度 10°C (32px)
LV_FONT_DECLARE(FontClock_64)   // 时钟

// ── 方案① 莫兰迪七彩（低饱和，同明度不同色相）──
#define COL_BG      0x14151a
#define COL_CARD    0x1c1d24
#define COL_CORAL   0xe8948a   // 时间
#define COL_APRICOT 0xe8c07a   // 天气
#define COL_CYAN    0x7fd8c8   // CPU
#define COL_MINT    0x8fd8a0   // 内存
#define COL_SKY     0x8fc0e8   // 磁盘
#define COL_LAVEN   0xb8a0e8   // GPU
#define COL_TEXT    0xf2ded9
#define COL_SUBTXT  0xa8a8b0
#define COL_DIM     0x6a6a74

#define SC 480
#define HOT_PREVIEW 5   // 主页热搜预览条数

// 双页：两个独立 screen，用 lv_scr_load 切换（LVGL 最稳的多页方式）
static uint8_t g_page = 0;
static lv_obj_t *g_home_scr, *g_news_scr, *g_work_scr, *g_detail_scr, *g_about_scr;

// 主页控件
static lv_obj_t *lb_clock, *lb_date, *lb_weather, *lb_temp, *lb_unit;
static lv_obj_t *lb_work, *lb_work_sub;
static lv_obj_t *lb_work_detail;   // 工时详情页的大字
static lv_obj_t *lb_work_detail_sub;
static lv_obj_t *work_chart;       // 工时折线图
static lv_chart_series_t *work_ser;
static lv_coord_t work_pts[WORK_HIST_DAYS];   // 图表数据(小时)，ext array 绑定用
static lv_obj_t *work_dtag[4];     // 图下方日期标签
static lv_obj_t *about_val[6];     // 关于页数值 label（timer 更新）

// 顶部下拉菜单
static lv_obj_t *g_menu_panel = NULL;   // 下拉胶囊面板
static lv_obj_t *g_menu_scrim = NULL;   // 半透明遮罩
static bool g_menu_open = false;
static char g_screen_ip[16] = "";

void gui_set_screen_ip(const char *ip) {
  strncpy(g_screen_ip, ip, sizeof(g_screen_ip) - 1);
  g_screen_ip[sizeof(g_screen_ip) - 1] = '\0';
}
static const char *WEEKDAY_CN[7] = {"周一","周二","周三","周四","周五","周六","周日"};

typedef struct {
  lv_obj_t *title;
  lv_obj_t *val;
  lv_obj_t *sub;
} Metric;

static Metric m_cpu, m_mem, m_disk, m_net;
static lv_obj_t *lb_hot[HOT_PREVIEW];
static lv_obj_t *news_row[HOTLIST_MAX];
// 详情页 label
static lv_obj_t *d_cpu, *d_gpu, *d_disk, *d_net;

static lv_obj_t *mk_label(lv_obj_t *parent, const lv_font_t *font, uint32_t color, const char *text) {
  lv_obj_t *lb = lv_label_create(parent);
  lv_obj_set_style_text_font(lb, font, 0);
  lv_obj_set_style_text_color(lb, lv_color_hex(color), 0);
  lv_label_set_text(lb, text);
  return lb;
}

static lv_obj_t *card(lv_obj_t *parent, lv_coord_t x, lv_coord_t y, lv_coord_t w, lv_coord_t h, uint32_t border) {
  lv_obj_t *c = lv_obj_create(parent);
  lv_obj_remove_style_all(c);
  lv_obj_set_pos(c, x, y);
  lv_obj_set_size(c, w, h);
  lv_obj_set_style_bg_color(c, lv_color_hex(COL_CARD), 0);
  lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(c, 10, 0);
  lv_obj_set_style_border_width(c, 1, 0);
  lv_obj_set_style_border_color(c, lv_color_hex(border), 0);
  lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE);
  return c;
}

// 建指标格：卡片内 flex 垂直均分，标题/主值/副值撑满，返回卡片供绑定点击
static lv_obj_t *build_metric(lv_obj_t *parent, lv_coord_t x, const char *title, uint32_t color, Metric *m, const lv_font_t *val_font) {
  lv_obj_t *c = card(parent, x, 148, 110, 132, color);
  lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(c, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_ver(c, 8, 0);
  m->title = mk_label(c, &FontGB2, color, title);
  m->val = mk_label(c, val_font, color, "--");
  m->sub = mk_label(c, &FontNum_16, COL_DIM, "--");   // 副值也用数字字体，统一风格
  return c;
}

// ── 翻页：点按切换屏幕（0主页 1新闻 2详情 3工时 4关于）──
static void show_page(uint8_t p) {
  g_page = p;
  lv_obj_t *s = (p == 0) ? g_home_scr : (p == 1) ? g_news_scr : (p == 2) ? g_detail_scr
              : (p == 3) ? g_work_scr : g_about_scr;
  lv_scr_load(s);
}
static void on_tap_news(lv_event_t *e) { if (lv_event_get_code(e) == LV_EVENT_CLICKED) show_page(1); }
static void on_tap_home(lv_event_t *e) { if (lv_event_get_code(e) == LV_EVENT_CLICKED) show_page(0); }
static void on_tap_detail(lv_event_t *e) { if (lv_event_get_code(e) == LV_EVENT_CLICKED) show_page(2); }
static void on_tap_work(lv_event_t *e) { if (lv_event_get_code(e) == LV_EVENT_CLICKED) show_page(3); }

// ── 顶部下拉菜单 ──
static void anim_y_cb(void *obj, int32_t v) { lv_obj_set_y((lv_obj_t *)obj, v); }

static void menu_close();   // 前向

// 菜单项点击：按 user_data 的页号切页（10=刷新，11=关于）
static void menu_item_cb(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  int act = (int)(intptr_t)lv_event_get_user_data(e);
  menu_close();
  if (act == 10) {
    // 刷新：让屏重新拉天气 + 等 PC 推
    extern void weather_begin();
    // 简单提示：无操作，PC 每2s推
  } else if (act == 11) {
    show_page(4);   // 关于页
  } else {
    show_page((uint8_t)act);
  }
}

static void menu_scrim_cb(lv_event_t *e) {
  if (lv_event_get_code(e) == LV_EVENT_CLICKED) menu_close();
}

static void menu_close() {
  if (!g_menu_open) return;
  g_menu_open = false;
  if (g_menu_scrim) { lv_obj_del(g_menu_scrim); g_menu_scrim = NULL; }
  if (g_menu_panel) {
    lv_anim_t a; lv_anim_init(&a);
    lv_anim_set_var(&a, g_menu_panel);
    lv_anim_set_exec_cb(&a, anim_y_cb);
    lv_anim_set_values(&a, lv_obj_get_y(g_menu_panel), -(lv_obj_get_height(g_menu_panel) + 4));
    lv_anim_set_time(&a, 200);
    lv_anim_set_ready_cb(&a, [](lv_anim_t *an) {
      lv_obj_del(g_menu_panel); g_menu_panel = NULL;
    });
    lv_anim_start(&a);
  }
}

static void menu_open(lv_obj_t *scr) {
  if (g_menu_open) { menu_close(); return; }
  g_menu_open = true;
  // 遮罩（点击关闭）
  g_menu_scrim = lv_obj_create(scr);
  lv_obj_remove_style_all(g_menu_scrim);
  lv_obj_set_size(g_menu_scrim, SC, SC);
  lv_obj_set_pos(g_menu_scrim, 0, 0);
  lv_obj_set_style_bg_color(g_menu_scrim, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(g_menu_scrim, LV_OPA_40, 0);
  lv_obj_add_flag(g_menu_scrim, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(g_menu_scrim, menu_scrim_cb, LV_EVENT_ALL, NULL);

  // 胶囊面板
  int panel_h = 92;
  g_menu_panel = lv_obj_create(scr);
  lv_obj_remove_style_all(g_menu_panel);
  lv_obj_set_size(g_menu_panel, SC - 16, panel_h);
  lv_obj_set_pos(g_menu_panel, 8, -panel_h);   // 从顶部外开始
  lv_obj_set_style_bg_color(g_menu_panel, lv_color_hex(0x20222c), 0);
  lv_obj_set_style_bg_opa(g_menu_panel, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(g_menu_panel, 18, 0);
  lv_obj_set_style_border_width(g_menu_panel, 1, 0);
  lv_obj_set_style_border_color(g_menu_panel, lv_color_hex(0x3a3f4c), 0);
  lv_obj_set_style_pad_all(g_menu_panel, 6, 0);
  lv_obj_clear_flag(g_menu_panel, LV_OBJ_FLAG_SCROLLABLE);

  // 横向可滑的图标行
  lv_obj_t *row = lv_obj_create(g_menu_panel);
  lv_obj_remove_style_all(row);
  lv_obj_set_size(row, SC - 16 - 12, panel_h - 12);
  lv_obj_set_pos(row, 0, 0);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(row, 8, 0);
  lv_obj_add_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scroll_dir(row, LV_DIR_HOR);

  extern const lv_img_dsc_t icon_home, icon_clock, icon_news, icon_chart, icon_refresh, icon_info;
  struct { const lv_img_dsc_t *ic; const char *nm; int act; } items[6] = {
    {&icon_home, "主页", 0}, {&icon_clock, "工时", 3}, {&icon_news, "热搜", 1},
    {&icon_chart, "详情", 2}, {&icon_refresh, "刷新", 10}, {&icon_info, "关于", 11},
  };
  for (int i = 0; i < 6; i++) {
    lv_obj_t *b = lv_obj_create(row);
    lv_obj_remove_style_all(b);
    lv_obj_set_size(b, 62, panel_h - 16);
    bool cur = (items[i].act < 10 && items[i].act == (int)g_page);   // 当前页高亮
    lv_obj_set_style_bg_color(b, lv_color_hex(cur ? 0x8fd8a0 : 0x343848), 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(b, 14, 0);
    lv_obj_set_style_border_width(b, 1, 0);
    lv_obj_set_style_border_color(b, lv_color_hex(cur ? 0x8fd8a0 : 0x464a5a), 0);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(b, menu_item_cb, LV_EVENT_ALL, (void *)(intptr_t)items[i].act);
    lv_obj_t *img = lv_img_create(b);
    lv_img_set_src(img, items[i].ic);
    if (cur) {   // 当前页：图标染深色配亮底
      lv_obj_set_style_img_recolor(img, lv_color_hex(0x10241a), 0);
      lv_obj_set_style_img_recolor_opa(img, LV_OPA_COVER, 0);
    }
    lv_obj_align(img, LV_ALIGN_TOP_MID, 0, 8);
    uint32_t nm_col = cur ? 0x10241a : COL_SUBTXT;
    lv_obj_t *nm = mk_label(b, &FontGB2, nm_col, items[i].nm);
    lv_obj_align(nm, LV_ALIGN_BOTTOM_MID, 0, -8);
  }

  // 滑下动画
  lv_anim_t a; lv_anim_init(&a);
  lv_anim_set_var(&a, g_menu_panel);
  lv_anim_set_exec_cb(&a, anim_y_cb);
  lv_anim_set_values(&a, -panel_h, 8);
  lv_anim_set_time(&a, 220);
  lv_anim_start(&a);
}

// 主页顶部把手点击 → 开/关菜单
static void menu_grab_cb(lv_event_t *e) {
  if (lv_event_get_code(e) == LV_EVENT_CLICKED) menu_open(g_home_scr);
}

// 顶部下滑手势 → 开菜单
static void menu_gesture_cb(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_GESTURE) return;
  lv_indev_t *indev = lv_indev_get_act();
  if (lv_indev_get_gesture_dir(indev) == LV_DIR_BOTTOM) {
    lv_indev_wait_release(indev);
    if (!g_menu_open) menu_open(g_home_scr);
  }
}

static void build_page_home() {
  g_home_scr = lv_obj_create(NULL);
  lv_obj_t *g_home = g_home_scr;
  lv_obj_set_size(g_home, SC, SC);
  lv_obj_set_style_pad_all(g_home, 0, 0);
  lv_obj_set_style_border_width(g_home, 0, 0);
  lv_obj_set_style_bg_color(g_home, lv_color_hex(COL_BG), 0);
  lv_obj_set_style_bg_opa(g_home, LV_OPA_COVER, 0);
  lv_obj_clear_flag(g_home, LV_OBJ_FLAG_SCROLLABLE);

  // 左上：时间+天气合并大卡 288×132（上：大时钟；下：左日期 右天气）
  lv_obj_t *c1 = card(g_home, 8, 8, 288, 132, COL_CORAL);
  lv_obj_t *t1 = mk_label(c1, &FontGB2, COL_CORAL, "时间  天气");
  lv_obj_align(t1, LV_ALIGN_TOP_LEFT, 16, 8);
  // 大时钟居中（64px，稍下移）
  lb_clock = mk_label(c1, &FontNum_64, COL_TEXT, "--:--");
  lv_obj_align(lb_clock, LV_ALIGN_TOP_MID, 0, 44);
  // 日期 左下
  lb_date = mk_label(c1, &FontGB2, COL_CORAL, "--");
  lv_obj_align(lb_date, LV_ALIGN_BOTTOM_LEFT, 16, -12);
  // 天气 右下：晴 10°C
  lb_weather = mk_label(c1, &FontWeather_24, COL_APRICOT, "--");
  lv_obj_align(lb_weather, LV_ALIGN_BOTTOM_RIGHT, -70, -10);
  lb_temp = mk_label(c1, &FontWeather_24, COL_APRICOT, "--");
  lv_obj_align_to(lb_temp, lb_weather, LV_ALIGN_OUT_RIGHT_MID, 6, 0);
  lb_unit = mk_label(c1, &FontWeather_24, COL_APRICOT, "°C");
  lv_obj_set_style_text_letter_space(lb_unit, -8, 0);
  lv_obj_align_to(lb_unit, lb_temp, LV_ALIGN_OUT_RIGHT_TOP, 0, 0);

  // 右上：今日工时卡 168×132
  lv_obj_t *cw = card(g_home, 304, 8, 168, 132, COL_MINT);
  lv_obj_add_flag(cw, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(cw, on_tap_work, LV_EVENT_ALL, NULL);
  lv_obj_t *wt = mk_label(cw, &FontGB2, COL_MINT, "今日工时");
  lv_obj_align(wt, LV_ALIGN_TOP_MID, 0, 10);
  lb_work = mk_label(cw, &FontNum_40, COL_MINT, "--");
  lv_obj_align(lb_work, LV_ALIGN_CENTER, 0, -4);
  lb_work_sub = mk_label(cw, &FontGB2, COL_DIM, "--");
  lv_obj_align(lb_work_sub, LV_ALIGN_BOTTOM_MID, 0, -14);

  // 四格：CPU / 内存 / 磁盘 / 网速（点击任意格进详情页）
  lv_obj_t *mc[4];
  mc[0] = build_metric(g_home, 8, "CPU", COL_CYAN, &m_cpu, &FontNum_40);
  mc[1] = build_metric(g_home, 126, "内存", COL_MINT, &m_mem, &FontNum_40);
  mc[2] = build_metric(g_home, 244, "磁盘", COL_SKY, &m_disk, &FontNum_40);
  mc[3] = build_metric(g_home, 362, "网速", COL_LAVEN, &m_net, &FontNum_30);
  for (int i = 0; i < 4; i++) {
    lv_obj_add_flag(mc[i], LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(mc[i], on_tap_detail, LV_EVENT_ALL, NULL);
  }

  // 热搜（可点击进新闻页）y=288 h=184，底部到 472
  lv_obj_t *cb = card(g_home, 8, 288, 464, 184, COL_MINT);
  lv_obj_add_flag(cb, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(cb, on_tap_news, LV_EVENT_ALL, NULL);
  lv_obj_t *ht = mk_label(cb, &FontGB2, COL_MINT, "热搜");
  lv_obj_align(ht, LV_ALIGN_TOP_LEFT, 16, 12);
  lv_obj_t *hint = mk_label(cb, &FontGB2, COL_APRICOT, "点此看全部 >");
  lv_obj_align(hint, LV_ALIGN_TOP_RIGHT, -16, 12);
  for (int i = 0; i < HOT_PREVIEW; i++) {
    lb_hot[i] = mk_label(cb, &FontGB2, COL_SUBTXT, "");
    lv_obj_set_width(lb_hot[i], 432);
    lv_label_set_long_mode(lb_hot[i], LV_LABEL_LONG_DOT);
    lv_obj_align(lb_hot[i], LV_ALIGN_TOP_LEFT, 16, 44 + i * 27);
  }

  // C 方案：无把手的顶部热区（点按或下滑都展开菜单），中央 3 个小点作暗示
  lv_obj_t *grab = lv_obj_create(g_home);
  lv_obj_remove_style_all(grab);
  lv_obj_set_size(grab, SC, 26);            // 顶部通栏热区
  lv_obj_align(grab, LV_ALIGN_TOP_MID, 0, 0);
  lv_obj_set_style_bg_opa(grab, LV_OPA_TRANSP, 0);
  lv_obj_add_flag(grab, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(grab, LV_OBJ_FLAG_FLOATING);
  lv_obj_add_event_cb(grab, menu_grab_cb, LV_EVENT_ALL, NULL);
  // 中央 3 个小点
  for (int i = 0; i < 3; i++) {
    lv_obj_t *dot = lv_obj_create(grab);
    lv_obj_remove_style_all(dot);
    lv_obj_set_size(dot, 6, 6);
    lv_obj_set_style_radius(dot, 3, 0);
    lv_obj_set_style_bg_color(dot, lv_color_hex(COL_SUBTXT), 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    lv_obj_align(dot, LV_ALIGN_TOP_MID, (i - 1) * 12, 6);
  }
  // 下滑手势展开菜单
  lv_obj_add_event_cb(g_home_scr, menu_gesture_cb, LV_EVENT_ALL, NULL);
}

static void build_page_news() {
  g_news_scr = lv_obj_create(NULL);
  lv_obj_t *g_news = g_news_scr;
  lv_obj_set_style_bg_color(g_news, lv_color_hex(COL_BG), 0);
  lv_obj_set_style_bg_opa(g_news, LV_OPA_COVER, 0);
  lv_obj_clear_flag(g_news, LV_OBJ_FLAG_SCROLLABLE);

  // 顶部标题条（可点击返回）
  lv_obj_t *c0 = card(g_news, 8, 8, 464, 56, COL_MINT);
  lv_obj_add_flag(c0, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(c0, on_tap_home, LV_EVENT_ALL, NULL);
  lv_obj_t *t = mk_label(c0, &FontGB2, COL_MINT, "微博热搜 TOP10");
  lv_obj_align(t, LV_ALIGN_LEFT_MID, 16, 0);
  lv_obj_t *r = mk_label(c0, &FontGB2, COL_APRICOT, "点此返回 >");
  lv_obj_align(r, LV_ALIGN_RIGHT_MID, -16, 0);

  lv_obj_t *lst = lv_obj_create(g_news);
  lv_obj_remove_style_all(lst);
  lv_obj_set_size(lst, 464, SC - 72 - 8);
  lv_obj_set_pos(lst, 8, 72);
  lv_obj_set_flex_flow(lst, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(lst, 10, 0);
  lv_obj_set_style_pad_left(lst, 10, 0);
  lv_obj_set_style_pad_top(lst, 8, 0);
  lv_obj_add_flag(lst, LV_OBJ_FLAG_SCROLLABLE);
  for (int i = 0; i < HOTLIST_MAX; i++) {
    news_row[i] = lv_label_create(lst);
    lv_obj_set_style_text_font(news_row[i], &FontGB2, 0);
    lv_obj_set_style_text_color(news_row[i],
        lv_color_hex(i < 2 ? COL_CORAL : COL_SUBTXT), 0);
    lv_label_set_long_mode(news_row[i], LV_LABEL_LONG_DOT);
    lv_obj_set_width(news_row[i], 438);
    lv_label_set_text(news_row[i], "");
  }
}

// ── 详情页：2×2 小卡，显示更多性能细节 ──
static void build_page_detail() {
  g_detail_scr = lv_obj_create(NULL);
  lv_obj_t *g = g_detail_scr;
  lv_obj_set_size(g, SC, SC);
  lv_obj_set_style_pad_all(g, 0, 0);
  lv_obj_set_style_bg_color(g, lv_color_hex(COL_BG), 0);
  lv_obj_set_style_bg_opa(g, LV_OPA_COVER, 0);
  lv_obj_clear_flag(g, LV_OBJ_FLAG_SCROLLABLE);

  // 顶部标题条（点击返回）
  lv_obj_t *c0 = card(g, 8, 8, 464, 48, COL_CORAL);
  lv_obj_add_flag(c0, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(c0, on_tap_home, LV_EVENT_ALL, NULL);
  lv_obj_t *t = mk_label(c0, &FontGB2, COL_CORAL, "性能详情");
  lv_obj_align(t, LV_ALIGN_LEFT_MID, 16, 0);
  lv_obj_t *r = mk_label(c0, &FontGB2, COL_CORAL, "点此返回 >");
  lv_obj_align(r, LV_ALIGN_RIGHT_MID, -16, 0);

  // 2×2 小卡：x=8/244 宽228(间距16)，y=64/262 高190
  struct { const char *title; uint32_t col; lv_obj_t **lb; lv_coord_t x, y; } cds[4] = {
    {"CPU",  COL_CYAN,   &d_cpu,  8,   64},
    {"GPU",  COL_LAVEN,  &d_gpu,  244, 64},
    {"磁盘", COL_SKY,    &d_disk, 8,   262},
    {"网络", COL_MINT,   &d_net,  244, 262},
  };
  for (int i = 0; i < 4; i++) {
    lv_obj_t *c = card(g, cds[i].x, cds[i].y, 228, 190, cds[i].col);
    // 边框用主色但降透明，保留颜色又不刺眼
    lv_obj_set_style_border_opa(c, LV_OPA_50, 0);
    // 卡片内 flex 垂直居中，标题+数据聚拢成一团
    lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(c, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(c, 10, 0);
    lv_obj_t *tt = mk_label(c, &FontGB2, cds[i].col, cds[i].title);
    lv_obj_t *lb = mk_label(c, &FontGB2, COL_TEXT, "--");
    lv_obj_set_width(lb, 208);
    lv_obj_set_style_text_align(lb, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(lb, LV_LABEL_LONG_WRAP);
    *cds[i].lb = lb;
  }
}

// ── 工时页：今日摘要 + 14天折线图 ──
static void build_page_work() {
  g_work_scr = lv_obj_create(NULL);
  lv_obj_t *g = g_work_scr;
  lv_obj_set_size(g, SC, SC);
  lv_obj_set_style_pad_all(g, 0, 0);
  lv_obj_set_style_bg_color(g, lv_color_hex(COL_BG), 0);
  lv_obj_set_style_bg_opa(g, LV_OPA_COVER, 0);
  lv_obj_clear_flag(g, LV_OBJ_FLAG_SCROLLABLE);

  // 顶部返回条
  lv_obj_t *c0 = card(g, 8, 8, 464, 48, COL_MINT);
  lv_obj_add_flag(c0, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(c0, on_tap_home, LV_EVENT_ALL, NULL);
  lv_obj_t *t = mk_label(c0, &FontGB2, COL_MINT, "工时统计");
  lv_obj_align(t, LV_ALIGN_LEFT_MID, 16, 0);
  lv_obj_t *r = mk_label(c0, &FontGB2, COL_MINT, "点此返回 >");
  lv_obj_align(r, LV_ALIGN_RIGHT_MID, -16, 0);

  // 今日摘要卡
  lv_obj_t *c1 = card(g, 8, 64, 464, 90, COL_CORAL);
  lv_obj_set_style_border_opa(c1, LV_OPA_50, 0);
  lb_work_detail = mk_label(c1, &FontNum_40, COL_CORAL, "--");
  lv_obj_align(lb_work_detail, LV_ALIGN_LEFT_MID, 22, -8);
  lv_obj_t *sub = mk_label(c1, &FontGB2, COL_SUBTXT, "");
  lv_obj_align(sub, LV_ALIGN_BOTTOM_LEFT, 24, -12);
  lb_work_detail_sub = sub;

  // 14 天折线图
  lv_obj_t *ct = mk_label(g, &FontGB2, COL_MINT, "近 14 天工时");
  lv_obj_align(ct, LV_ALIGN_TOP_LEFT, 20, 160);
  work_chart = lv_chart_create(g);
  lv_obj_set_pos(work_chart, 44, 186);
  lv_obj_set_size(work_chart, 420, 250);
  lv_chart_set_type(work_chart, LV_CHART_TYPE_LINE);
  lv_chart_set_point_count(work_chart, WORK_HIST_DAYS);
  lv_chart_set_range(work_chart, LV_CHART_AXIS_PRIMARY_Y, 0, 12);   // 0-12 小时
  lv_chart_set_div_line_count(work_chart, 4, 0);
  lv_obj_set_style_bg_color(work_chart, lv_color_hex(COL_CARD), 0);
  lv_obj_set_style_bg_opa(work_chart, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(work_chart, 1, 0);
  lv_obj_set_style_border_color(work_chart, lv_color_hex(0x2a2e38), 0);
  lv_obj_set_style_radius(work_chart, 10, 0);
  lv_obj_set_style_size(work_chart, 0, LV_PART_INDICATOR);   // 不画点
  lv_obj_set_style_line_width(work_chart, 3, LV_PART_ITEMS);
  work_ser = lv_chart_add_series(work_chart, lv_color_hex(COL_MINT), LV_CHART_AXIS_PRIMARY_Y);
  for (int i = 0; i < WORK_HIST_DAYS; i++) work_pts[i] = 0;
  lv_chart_set_ext_y_array(work_chart, work_ser, work_pts);   // 绑定一次，之后只改数组

  // Y 轴刻度：0 / 6 / 12 小时
  lv_chart_set_axis_tick(work_chart, LV_CHART_AXIS_PRIMARY_Y, 0, 0, 3, 1, false, 44);
  lv_obj_set_style_text_font(work_chart, &FontNum_16, LV_PART_TICKS);
  lv_obj_set_style_text_color(work_chart, lv_color_hex(COL_DIM), LV_PART_TICKS);

  // 图下方日期标签（4 个锚点对应第 0/4/9/13 天，内容 timer 动态填）
  lv_coord_t dx[4] = {44, 168, 320, 424};
  for (int i = 0; i < 4; i++) {
    work_dtag[i] = mk_label(g, &FontGB2, COL_DIM, "");
    lv_obj_set_pos(work_dtag[i], dx[i], 444);
  }
}

// 关于页：居中标题 + 信息表 + 返回按钮
static const char *ABOUT_KEYS[6] = {"IP 地址", "WiFi 信号", "芯片", "Flash / PSRAM", "空闲堆内存", "运行时长"};

static void update_about() {
  extern void sys_info(int which, char *buf, int len);
  char v[32];
  for (int i = 0; i < 6; i++) {
    if (!about_val[i]) continue;
    if (i == 0) snprintf(v, 32, "%s", g_screen_ip[0] ? g_screen_ip : "--");
    else { int map[5] = {5, 0, 1, 3, 4}; sys_info(map[i - 1], v, 32); }
    if (strcmp(lv_label_get_text(about_val[i]), v) != 0)   // 变了才刷
      lv_label_set_text(about_val[i], v);
  }
}

static void build_page_about() {
  g_about_scr = lv_obj_create(NULL);
  lv_obj_t *g = g_about_scr;
  lv_obj_set_size(g, SC, SC);
  lv_obj_set_style_pad_all(g, 0, 0);
  lv_obj_set_style_bg_color(g, lv_color_hex(0x14151a), 0);
  lv_obj_set_style_bg_opa(g, LV_OPA_COVER, 0);
  lv_obj_clear_flag(g, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *title = mk_label(g, &FontGB2, COL_TEXT, "ESP32-S3 信息副屏");
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 22);
  lv_obj_t *sub = mk_label(g, &FontGB2, COL_DIM, "v1.0  2026-11-20");
  lv_obj_align(sub, LV_ALIGN_TOP_MID, 0, 50);

  // 信息卡
  lv_obj_t *tbl = card(g, 16, 82, SC - 32, 252, 0x2e3140);
  lv_obj_set_style_bg_color(tbl, lv_color_hex(0x1a1c24), 0);
  lv_obj_set_style_border_opa(tbl, LV_OPA_50, 0);
  for (int i = 0; i < 6; i++) {
    int y = 12 + i * 40;
    lv_obj_t *k = mk_label(tbl, &FontGB2, COL_SUBTXT, ABOUT_KEYS[i]);
    lv_obj_set_pos(k, 14, y);
    about_val[i] = mk_label(tbl, &FontGB2, COL_TEXT, "--");   // 中文兼容字体
    lv_obj_align(about_val[i], LV_ALIGN_TOP_RIGHT, -14, y);
    if (i < 5) {   // 行分隔线
      lv_obj_t *ln = lv_obj_create(tbl);
      lv_obj_remove_style_all(ln);
      lv_obj_set_size(ln, SC - 32 - 28, 1);
      lv_obj_set_pos(ln, 14, y + 32);
      lv_obj_set_style_bg_color(ln, lv_color_hex(0xffffff), 0);
      lv_obj_set_style_bg_opa(ln, LV_OPA_10, 0);
    }
  }

  // 返回按钮
  lv_obj_t *btn = card(g, 16, 352, SC - 32, 52, COL_APRICOT);
  lv_obj_set_style_bg_color(btn, lv_color_hex(0x2a2418), 0);
  lv_obj_set_style_border_color(btn, lv_color_hex(COL_APRICOT), 0);
  lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(btn, on_tap_home, LV_EVENT_ALL, NULL);
  lv_obj_t *bl = mk_label(btn, &FontGB2, COL_APRICOT, "点此返回  >");
  lv_obj_center(bl);

  update_about();
}

static void build_ui() {
  build_page_home();
  build_page_news();
  build_page_detail();
  build_page_work();
  build_page_about();
  show_page(0);
}
static void gui_timer_cb(lv_timer_t *t) {
  // OTA 升级中：黑底 + "升级中" + 进度条
  static lv_obj_t *ota_mask = NULL;
  static lv_obj_t *ota_bar = NULL;
  static lv_obj_t *ota_lbl = NULL;
  if (g_ota_updating) {
    if (!ota_mask) {
      ota_mask = lv_obj_create(lv_scr_act());
      lv_obj_remove_style_all(ota_mask);
      lv_obj_set_size(ota_mask, SC, SC);
      lv_obj_set_pos(ota_mask, 0, 0);
      lv_obj_set_style_bg_color(ota_mask, lv_color_hex(0x000000), 0);
      lv_obj_set_style_bg_opa(ota_mask, LV_OPA_COVER, 0);
      lv_obj_t *t1 = mk_label(ota_mask, &FontGB2, COL_CYAN, "固件升级中...");
      lv_obj_align(t1, LV_ALIGN_CENTER, 0, -50);
      ota_bar = lv_bar_create(ota_mask);
      lv_obj_set_size(ota_bar, 320, 16);
      lv_obj_align(ota_bar, LV_ALIGN_CENTER, 0, 6);
      lv_bar_set_range(ota_bar, 0, 100);
      lv_obj_set_style_bg_color(ota_bar, lv_color_hex(0x222222), LV_PART_MAIN);
      lv_obj_set_style_bg_color(ota_bar, lv_color_hex(COL_CYAN), LV_PART_INDICATOR);
      ota_lbl = mk_label(ota_mask, &FontGB2, COL_TEXT, "0%");
      lv_obj_align(ota_lbl, LV_ALIGN_CENTER, 0, 46);
    }
    lv_bar_set_value(ota_bar, g_ota_progress, LV_ANIM_OFF);
    lv_label_set_text_fmt(ota_lbl, "%d%%", g_ota_progress);
    return;   // 升级时不刷新正常 UI
  } else if (ota_mask) {
    lv_obj_del(ota_mask); ota_mask = NULL; ota_bar = NULL; ota_lbl = NULL;
  }

  // 关于页数据更新（本地 ESP 数据，不依赖 PC）；仅在关于页时刷
  if (g_page == 4) update_about();

  if (!pcmon_is_fresh()) return;

  lv_label_set_text(lb_clock, g_sysData.time_str);
  int wd = g_sysData.weekday;
  if (wd < 0 || wd > 6) wd = 0;
  lv_label_set_text_fmt(lb_date, "%d/%d %s", g_sysData.month, g_sysData.day, WEEKDAY_CN[wd]);
  // 天气用屏端自取的 g_weather：描述小字 + 温度大字
  if (g_weather.valid) {
    lv_label_set_text(lb_weather, g_weather.desc);
    lv_label_set_text_fmt(lb_temp, "%d", g_weather.temp);
  } else {
    lv_label_set_text(lb_weather, "--");
    lv_label_set_text(lb_temp, "--");
  }

  int cpu = (int)(g_sysData.cpu + 0.5f);
  int mem = (int)(g_sysData.mem + 0.5f);
  int dsk = (int)(g_sysData.disk + 0.5f);
  char tmp[32];
  // CPU：主 % 副 频率（snprintf 支持浮点，lv_label_set_text_fmt 不支持）
  lv_label_set_text_fmt(m_cpu.val, "%d%%", cpu);
  snprintf(tmp, sizeof(tmp), "%.2fG", g_sysData.cpu_freq / 1000.0f);
  lv_label_set_text(m_cpu.sub, tmp);
  // 内存：主 % 副 已用/总 GB
  lv_label_set_text_fmt(m_mem.val, "%d%%", mem);
  snprintf(tmp, sizeof(tmp), "%.1f/%.1fG", g_sysData.mem_used_gb, g_sysData.mem_total_gb);
  lv_label_set_text(m_mem.sub, tmp);
  // 磁盘：主 读速度 副 写速度
  {
    char r[10], w[10];
    if (g_sysData.disk_read >= 1024) snprintf(r, sizeof(r), "%.0fM", g_sysData.disk_read / 1024); else snprintf(r, sizeof(r), "%.0fK", g_sysData.disk_read);
    if (g_sysData.disk_write >= 1024) snprintf(w, sizeof(w), "%.0fM", g_sysData.disk_write / 1024); else snprintf(w, sizeof(w), "%.0fK", g_sysData.disk_write);
    lv_label_set_text(m_disk.val, r);
    snprintf(tmp, sizeof(tmp), "W%s", w);
    lv_label_set_text(m_disk.sub, tmp);
  }
  // 网速：主 下行 副 上行
  char dn[12], up[12];
  if (g_sysData.net_down >= 1024) snprintf(dn, sizeof(dn), "%.1fM", g_sysData.net_down / 1024);
  else snprintf(dn, sizeof(dn), "%.0fK", g_sysData.net_down);
  if (g_sysData.net_up >= 1024) snprintf(up, sizeof(up), "%.1fM", g_sysData.net_up / 1024);
  else snprintf(up, sizeof(up), "%.0fK", g_sysData.net_up);
  lv_label_set_text(m_net.val, dn);
  lv_label_set_text_fmt(m_net.sub, "%s up", up);

  for (int i = 0; i < HOT_PREVIEW; i++) {
    if (i < g_sysData.hot_count)
      lv_label_set_text_fmt(lb_hot[i], "%d. %s", i + 1, g_sysData.hot[i]);
    else
      lv_label_set_text(lb_hot[i], "");
  }
  for (int i = 0; i < HOTLIST_MAX; i++) {
    if (i < g_sysData.hot_count)
      lv_label_set_text_fmt(news_row[i], "[%02d] %s", i + 1, g_sysData.hot[i]);
    else
      lv_label_set_text(news_row[i], "");
  }

  // 详情页
  char d1[12], d2[12];
  if (g_sysData.net_down >= 1024) snprintf(d1, sizeof(d1), "%.1fM/s", g_sysData.net_down / 1024); else snprintf(d1, sizeof(d1), "%.0fK/s", g_sysData.net_down);
  if (g_sysData.net_up >= 1024) snprintf(d2, sizeof(d2), "%.1fM/s", g_sysData.net_up / 1024); else snprintf(d2, sizeof(d2), "%.0fK/s", g_sysData.net_up);
  char dc[80];
  snprintf(dc, sizeof(dc), "占用: %d%%\n频率: %.2f GHz\n温度: %d C",
      (int)(g_sysData.cpu + 0.5f), g_sysData.cpu_freq / 1000.0f, (int)(g_sysData.cpu_temp + 0.5f));
  lv_label_set_text(d_cpu, dc);
  lv_label_set_text_fmt(d_gpu, "占用: %d%%\n温度: %d C\n显存: %d/%d MB\n功耗: %d W",
      (int)(g_sysData.gpu + 0.5f), (int)(g_sysData.gpu_temp + 0.5f),
      g_sysData.gpu_mem_used, g_sysData.gpu_mem_total, g_sysData.gpu_power);
  char dr[10], dw[10];
  if (g_sysData.disk_read >= 1024) snprintf(dr, sizeof(dr), "%.1fM", g_sysData.disk_read / 1024); else snprintf(dr, sizeof(dr), "%.0fK", g_sysData.disk_read);
  if (g_sysData.disk_write >= 1024) snprintf(dw, sizeof(dw), "%.1fM", g_sysData.disk_write / 1024); else snprintf(dw, sizeof(dw), "%.0fK", g_sysData.disk_write);
  snprintf(dc, sizeof(dc), "占用: %d%%\n已用: %d GB\n总计: %d GB\n读: %s/s\n写: %s/s",
      (int)(g_sysData.disk + 0.5f), g_sysData.disk_used_gb, g_sysData.disk_total_gb, dr, dw);
  lv_label_set_text(d_disk, dc);
  lv_label_set_text_fmt(d_net, "下行: %s\n上行: %s", d1, d2);

  // 工时：主页卡 + 工时页
  if (lb_work) {
    int h = g_sysData.work_minutes / 60, mi = g_sysData.work_minutes % 60;
    lv_label_set_text_fmt(lb_work, "%d:%02d", h, mi);
    if (g_sysData.work_active)
      lv_label_set_text_fmt(lb_work_sub, "%s-%s", g_sysData.work_start,
          (g_sysData.work_end[0] && g_sysData.work_end[0] != '-') ? g_sysData.work_end : "上班中");
    else
      lv_label_set_text(lb_work_sub, "未上班");
  }
  if (lb_work_detail) {
    char wb[32];
    int h = g_sysData.work_minutes / 60, mi = g_sysData.work_minutes % 60;
    snprintf(wb, sizeof(wb), "%d:%02d", h, mi);   // 纯数字，FontNum_40 才有
    lv_label_set_text(lb_work_detail, wb);
    lv_label_set_text_fmt(lb_work_detail_sub, "上班 %s   下班 %s",
        g_sysData.work_start[0] ? g_sysData.work_start : "--:--",
        (g_sysData.work_end[0] && g_sysData.work_end[0] != '-') ? g_sysData.work_end : "进行中");
  }
  if (work_chart) {
    bool changed = false;
    for (int i = 0; i < WORK_HIST_DAYS; i++) {
      lv_coord_t v = g_sysData.work_hist[i] / 60;
      if (work_pts[i] != v) { work_pts[i] = v; changed = true; }
    }
    if (changed) lv_chart_refresh(work_chart);   // 只在数据变化时重绘，避免闪烁
    // 日期标签：第 0 / 4 / 9 / 13 天
    int idx[4] = {0, 4, 9, 13};
    for (int k = 0; k < 4; k++) {
      int i = idx[k];
      if (i == WORK_HIST_DAYS - 1) {
        if (strcmp(lv_label_get_text(work_dtag[k]), "今天") != 0)
          lv_label_set_text(work_dtag[k], "今天");
      } else if (g_sysData.work_hist_date[i][0]) {
        if (strcmp(lv_label_get_text(work_dtag[k]), g_sysData.work_hist_date[i]) != 0)
          lv_label_set_text(work_dtag[k], g_sysData.work_hist_date[i]);
      }
    }
  }
}

void load_ui() {
#ifdef DISPLAY_DIAG
  // LVGL 渲染诊断：全屏红 + 白十字 + 四角标记，验证 LVGL flush 路径是否偏移
  lv_obj_t *scr = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(scr, lv_color_hex(0xF800), 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  lv_obj_t *h = lv_obj_create(scr);
  lv_obj_remove_style_all(h);
  lv_obj_set_size(h, 480, 6);
  lv_obj_set_style_bg_color(h, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_bg_opa(h, LV_OPA_COVER, 0);
  lv_obj_set_pos(h, 0, 237);
  lv_obj_t *v = lv_obj_create(scr);
  lv_obj_remove_style_all(v);
  lv_obj_set_size(v, 6, 480);
  lv_obj_set_style_bg_color(v, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_bg_opa(v, LV_OPA_COVER, 0);
  lv_obj_set_pos(v, 237, 0);
  // 四角 20x20 白块（应贴四角）
  lv_coord_t corners[4][2] = {{0,0},{460,0},{0,460},{460,460}};
  for (int i = 0; i < 4; i++) {
    lv_obj_t *c = lv_obj_create(scr);
    lv_obj_remove_style_all(c);
    lv_obj_set_size(c, 20, 20);
    lv_obj_set_style_bg_color(c, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
    lv_obj_set_pos(c, corners[i][0], corners[i][1]);
  }
  lv_scr_load(scr);
  return;
#endif
  build_ui();
#ifdef OVERLAY_REF
  // 在主页上叠加参考线，看 UI 相对 480 边界是否偏移
  lv_obj_t *ref = lv_obj_create(g_home_scr);
  lv_obj_remove_style_all(ref);
  lv_obj_set_size(ref, 480, 480);
  lv_obj_set_pos(ref, 0, 0);
  lv_obj_set_style_border_width(ref, 2, 0);
  lv_obj_set_style_border_color(ref, lv_color_hex(0xFF00FF), 0);  // 品红外框贴边
  lv_obj_clear_flag(ref, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_t *cx = lv_obj_create(g_home_scr);                      // 中心竖线
  lv_obj_remove_style_all(cx);
  lv_obj_set_size(cx, 2, 480); lv_obj_set_pos(cx, 239, 0);
  lv_obj_set_style_bg_color(cx, lv_color_hex(0xFF00FF), 0); lv_obj_set_style_bg_opa(cx, LV_OPA_COVER, 0);
  lv_obj_t *cy = lv_obj_create(g_home_scr);                      // 中心横线
  lv_obj_remove_style_all(cy);
  lv_obj_set_size(cy, 480, 2); lv_obj_set_pos(cy, 0, 239);
  lv_obj_set_style_bg_color(cy, lv_color_hex(0xFF00FF), 0); lv_obj_set_style_bg_opa(cy, LV_OPA_COVER, 0);
  lv_coord_t corners[4][2] = {{0,0},{466,0},{0,466},{466,466}};
  for (int i = 0; i < 4; i++) {                                  // 四角橙块
    lv_obj_t *c = lv_obj_create(g_home_scr);
    lv_obj_remove_style_all(c);
    lv_obj_set_size(c, 14, 14); lv_obj_set_pos(c, corners[i][0], corners[i][1]);
    lv_obj_set_style_bg_color(c, lv_color_hex(0xFF9900), 0); lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
  }
#endif
  lv_timer_create(gui_timer_cb, 500, NULL);
}

void gui_debug_page(int p) { show_page((uint8_t)p); }
void gui_debug_menu() { menu_open(g_home_scr); }

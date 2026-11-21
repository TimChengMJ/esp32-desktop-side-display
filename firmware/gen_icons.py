"""生成菜单图标：用 Pillow 画简易线条图标 -> LVGL C 数组(RGB565+alpha)
   每图标 1 位 alpha 蒙版 + 颜色，转成 lv_img_dsc_t (true color + alpha)
   简化：直接存 ARGB，用 lv_img 显示。这里用 LV_IMG_CF_TRUE_COLOR_ALPHA 格式(RGB565 2字节 + A 1字节)
"""
import math
from PIL import Image, ImageDraw

SZ = 40
COL = (240, 222, 217)   # 图标颜色(偏白)

def new_canvas():
    im = Image.new("RGBA", (SZ, SZ), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    return im, d

def icon_home():
    im, d = new_canvas()
    d.polygon([(20, 6), (35, 19), (32, 19), (32, 33), (24, 33), (24, 25),
               (16, 25), (16, 33), (8, 33), (8, 19), (5, 19)], fill=COL)
    return im

def icon_clock():
    im, d = new_canvas()
    d.ellipse([6, 6, 34, 34], outline=COL, width=3)
    d.line([(20, 20), (20, 12)], fill=COL, width=3)   # 时针
    d.line([(20, 20), (27, 24)], fill=COL, width=3)   # 分针
    return im

def icon_news():
    im, d = new_canvas()
    d.rectangle([6, 9, 34, 31], outline=COL, width=2)
    d.line([(10, 15), (30, 15)], fill=COL, width=2)
    d.line([(10, 20), (30, 20)], fill=COL, width=2)
    d.line([(10, 25), (22, 25)], fill=COL, width=2)
    return im

def icon_chart():
    im, d = new_canvas()
    d.line([(6, 33), (34, 33)], fill=COL, width=2)   # 底轴
    d.rectangle([9, 20, 15, 33], fill=COL)
    d.rectangle([17, 13, 23, 33], fill=COL)
    d.rectangle([25, 22, 31, 33], fill=COL)
    return im

def icon_refresh():
    im, d = new_canvas()
    d.arc([8, 8, 32, 32], start=40, end=320, fill=COL, width=3)
    # 箭头
    d.polygon([(30, 8), (36, 14), (28, 15)], fill=COL)
    return im

def icon_info():
    im, d = new_canvas()
    d.ellipse([6, 6, 34, 34], outline=COL, width=3)
    d.ellipse([18, 11, 22, 15], fill=COL)   # 点
    d.line([(20, 19), (20, 30)], fill=COL, width=3)   # 竖线
    return im

icons = [
    ("home", icon_home()), ("clock", icon_clock()), ("news", icon_news()),
    ("chart", icon_chart()), ("refresh", icon_refresh()), ("info", icon_info()),
]

def to_lvgl_c(name, im):
    """输出 LVGL true_color_alpha C 数组（RGB565 小端 2字节 + alpha 1字节）"""
    px = im.load()
    out = []
    for y in range(SZ):
        for x in range(SZ):
            r, g, b, a = px[x, y]
            r5 = r >> 3; g6 = g >> 2; b5 = b >> 3
            c565 = (r5 << 11) | (g6 << 5) | b5
            out.append(c565 & 0xFF)          # 低字节
            out.append((c565 >> 8) & 0xFF)   # 高字节
            out.append(a)                     # alpha
    data = ",".join(str(v) for v in out)
    return f"""static const uint8_t {name}_map[] = {{{data}}};
const lv_img_dsc_t {name} = {{
  .header.cf = LV_IMG_CF_TRUE_COLOR_ALPHA,
  .header.always_zero = 0,
  .header.reserved = 0,
  .header.w = {SZ},
  .header.h = {SZ},
  .data_size = {SZ*SZ*3},
  .data = {name}_map,
}};
"""

with open("icons.c", "w", encoding="utf-8") as f:
    f.write('#include "lvgl.h"\n#include <stdint.h>\n\n')
    for name, im in icons:
        im.save(f"../simulator/build/icon_{name}.png")  # 预览
        f.write(to_lvgl_c(f"icon_{name}", im))
        f.write("\n")
print("icons.c generated")

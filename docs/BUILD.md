# 编译与烧录指南（Windows）

本文从零开始，把一台干净的 Windows 电脑配到能编译、能烧录、能 OTA 升级这块屏的程度。

内容包括：

1. [环境准备](#1-环境准备)
2. [安装依赖库](#2-安装依赖库)
3. [配置 LVGL（`lv_conf.h`）](#3-配置-lvgllv_confh)
4. [填写 WiFi 与引脚](#4-填写-wifi-与引脚)
5. [方式一：Arduino IDE 图形界面](#5-方式一arduino-ide-图形界面)
6. [方式二：arduino-cli 命令行](#6-方式二arduino-cli-命令行推荐)
7. [首次烧录（USB 串口）](#7-首次烧录usb-串口)
8. [后续升级（局域网 OTA）](#8-后续升级局域网-ota)
9. [排错](#9-排错)
10. [附录：分区表说明](#附录分区表说明)

---

## 1. 环境准备

### 1.1 安装 Arduino IDE 2.x

从 <https://www.arduino.cc/en/software> 下载安装。装完打开，**首次启动需要联网**，它会自动下载一些依赖。

> 如果只想用命令行，可跳过这步直接看 [1.3 arduino-cli](#13-用-arduino-cli-替代-可跳过-11-和-12)。但 IDE 装板子支持包最省事，建议先装。

### 1.2 安装 ESP32 开发板支持包（关键版本）

打开 **文件 → 首选项**，在「附加开发板管理器网址」填入：

```
https://espressif.github.io/arduino-esp32/package_esp32_index.json
```

然后 **工具 → 开发板 → 开发板管理器**，搜索 `esp32`，找到 **esp32 by Espressif Systems**。

**必须选 2.0.17 版本**，不要装 3.x：

- 3.x 改了 RGB 面板驱动的 API、改了分区和 PSRAM 的行为，本项目代码无法直接编译
- 本项目的参考实现和全部验证都在 2.0.17 上完成

如果开发板管理器里只显示最新版，点版本号下拉框可以选历史版本。

### 1.3 用 arduino-cli 替代（可跳过 1.1 和 1.2）

`arduino-cli` 是同一套工具链的命令行版，适合脚本化、CI、或者不想装 IDE。

**安装**（PowerShell 或 CMD）：

```powershell
winget install ArduinoSA.CLI
```

或从 <https://github.com/arduino/arduino-cli/releases> 下载 `arduino-cli_x.x.x_Windows_64bit.zip`，解压后把 `arduino-cli.exe` 所在目录加进 `PATH`。

**安装 ESP32 核心到指定版本**：

```powershell
arduino-cli config init
arduino-cli config add board_manager.additional_urls https://espressif.github.io/arduino-esp32/package_esp32_index.json
arduino-cli core update-index
arduino-cli core install esp32:esp32@2.0.17
```

验证：

```powershell
arduino-cli core list
```

应该看到 `esp32:esp32  2.0.17`。

### 1.4 安装串口驱动

多数 ESP32-S3 开发板用 CH340 或 CP210x 芯片。接上板子后如果「设备管理器」里看到黄色感叹号，安装对应驱动：

- CH340：<http://www.wch.cn/downloads/CH341SER_EXE.html>
- CP210x：<https://www.silabs.com/developers/usb-to-uart-bridge-vcp-drivers>

安装后设备管理器「端口 (COM 和 LPT)」下会出现 `USB-SERIAL CH340 (COMx)` 之类的条目，记住这个 COM 号。

---

## 2. 安装依赖库

### 2.1 库清单

| 库名 | 版本 | 用途 | 备注 |
|---|---|---|---|
| **lvgl** | **8.4.0** | UI 框架 | 不能升 9.x，API 大改 |
| **GFX Library for Arduino**（Arduino_GFX） | **1.4.7** | ST7701 RGB 面板驱动 | 新版改了 RGB 面板构造参数 |
| **TAMC_GT911** | 1.0.2 | GT911 触摸 | |
| **ArduinoJson** | **6.21.5** | 解析 PC 推送的 JSON | 7.x 的 `DynamicJsonDocument` 已在 7.x 中移除 |

> 版本很关键。尤其是 lvgl 8 与 9、ArduinoJson 6 与 7，跨大版本都会编译不过。

### 2.2 Arduino IDE 安装

**工具 → 管理库**，搜索库名，在版本下拉框里选定上表版本后点「安装」。

### 2.3 arduino-cli 安装

```powershell
arduino-cli lib install "lvgl@8.4.0"
arduino-cli lib install "GFX Library for Arduino@1.4.7"
arduino-cli lib install "TAMC_GT911@1.0.2"
arduino-cli lib install "ArduinoJson@6.21.5"
```

验证：

```powershell
arduino-cli lib list
```

---

## 3. 配置 LVGL（`lv_conf.h`）

LVGL 需要一份 `lv_conf.h`。Arduino 环境下的规则是：**放在 Arduino 库目录顶层**（与 `libraries` 下各库文件夹同级的位置），LVGL 编译时会去找它。

### 3.1 找到库目录

| 方式 | 路径 |
|---|---|
| Arduino IDE | 菜单 **文件 → 首选项**，看「项目文件夹位置」，库目录是其中的 `libraries` 子目录 |
| Windows 默认 | `C:\Users\<用户名>\Documents\Arduino\libraries` |

### 3.2 放置 `lv_conf.h`

从 LVGL 库自带的模板复制一份：

```powershell
# 把 <libraries> 换成你的实际库目录
copy "<libraries>\lvgl-8.4.0\lv_conf_template.h" "<libraries>\lv_conf.h"
```

> 注意目标是 `libraries\lv_conf.h`，**不是** `libraries\lvgl-8.4.0\lv_conf.h`。

### 3.3 修改三个关键配置

打开 `lv_conf.h`，改这几处：

**① 打开配置开关**（文件开头，第 15 行左右）：

```c
#if 0    /* 改成 1 */
```

改为

```c
#if 1
```

**② 内存池大小（必须改，这是踩过的坑）**：

```c
#define LV_MEM_SIZE (48U * 1024U)
```

改为

```c
#define LV_MEM_SIZE (96U * 1024U)
```

> **为什么必须改**：5 个页面常驻内存池约 40KB，打开下拉菜单还要再创建遮罩、面板和 6 个按钮。48KB 会在第 6 个按钮时耗尽，LVGL 内部陷入死循环，表现为**点下拉就卡死**。96KB 已验证够用。若以后页面继续增加，可再上调。

**③ 关闭浮点格式化**（必须保持关闭）：

```c
#define LV_SPRINTF_USE_FLOAT 0
```

> 保持 `0`。本项目所有数值都用整数格式或 `snprintf` 拼字符串。打开这个选项会让固件增大，且之前的显示问题正是在这个配置下排查出来的。

**④ 确认颜色深度**：

```c
#define LV_COLOR_DEPTH 16
```

RGB565，与 ST7701 面板一致。

---

## 4. 填写 WiFi 与引脚

打开 [`firmware/config.h`](../firmware/config.h)：

```c
#define WIFI_SSID "YOUR_WIFI_SSID"
#define WIFI_PASS "YOUR_WIFI_PASSWORD"
```

改成你自己的 2.4GHz WiFi。**ESP32 不支持 5GHz**。

> 这一项配置错误是最常见的「烧录成功但屏幕一直显示旧内容 / 连不上」的原因。串口日志会打印 `WiFi: connecting to xxx ...`，连不上会打印 `connect failed`。

引脚定义不用动，除非你自己画了板子。默认值对应本项目的接线（见 `config.h` 注释）。

---

## 5. 方式一：Arduino IDE 图形界面

### 5.1 打开工程

用 Arduino IDE 打开 `firmware/ESP32_SCREEN.ino`。

IDE 会自动把同目录的 `.h` / `.cpp` / `.c` 作为这个 sketch 的一部分载入。**不要**把 `firmware` 里的文件拆开放到别处，LVGL 和字库都是按同目录解析的。

### 5.2 选开发板

**工具 → 开发板 → esp32 → ESP32S3 Dev Module**

然后按下表设置 **工具** 菜单里的各项：

| 菜单项 | 设置值 | 说明 |
|---|---|---|
| **Board** | ESP32S3 Dev Module | |
| **USB CDC On Boot** | **Disabled** | 开启会改变串口行为，本项目按 Disabled 验证 |
| **CPU Frequency** | 240MHz (WiFi) | |
| **Flash Mode** | **QIO 80MHz** | |
| **Flash Size** | **8MB (64Mb)** | N8R8 的部分 |
| **Partition Scheme** | **8M Flash (3MB APP / 1.5MB SPIFFS)** 或自定义 | 见下方说明 |
| **PSRAM** | **OPI PSRAM** | N8R8 必须选 OPI，不能选 QSPI |
| **Upload Speed** | 921600 | 烧录更快 |
| **USB DFU On Boot** | Disabled | |
| **Erase All Flash Before Sketch Upload** | Disabled | |
| **Upload Mode** | UART0 / Hardware CDC | 按你的板子接线，一般默认 |

#### 关于 Partition Scheme（重要）

固件当前约 **1.58MB**。ESP32 核心 2.0.17 自带的分区方案里，`min_spiffs` 的 app 槽是 1.875MB，剩余空间只够再塞一点小功能。

本项目提供了一份 **8MB 双 OTA 分区表** [`firmware/partitions.csv`](../firmware/partitions.csv)，每个 app 槽 3.75MB：

```
app0  0x20000   3.75MB   ← 当前运行的固件
app1  0x3E0000  3.75MB   ← OTA 升级时写入备用槽
```

**让自定义分区表生效的做法**：把 `partitions.csv` **直接放在 sketch 目录**（与 `.ino` 同级）。ESP32 核心的预编译钩子会自动优先使用它。

```
ESP32_SCREEN/
├── ESP32_SCREEN.ino
├── partitions.csv     ← 放这里
└── ...
```

> ⚠️ **菜单里的 Partition Scheme 仍然要选一个**（选 `min_spiffs` 即可）。核心包用该菜单项决定编译期的 `upload.maximum_size`，**即使自定义 csv 已生效，菜单里的上限也不会自动变成 3.75MB**。所以用 Arduino IDE 时，如果编译报 `text section exceeds available space in board`，改用下面的命令行方式编译，或者临时选 `Huge APP (3MB No OTA)` 系列——但那会牺牲 OTA。

命令行能精确覆盖上限，见 [6.2 编译](#62-编译)。这也是**推荐用命令行编译**的原因之一。

### 5.3 选端口并上传

**工具 → 端口** 选你的 COM 口（如 `COM5`），点工具栏的 **→ 上传** 按钮。

第一次会编译全部文件（包括 3.3MB 的字库），**可能要 3~5 分钟**。看到 `Hash of data verified.` 即成功。

### 5.4 看日志

**工具 → 串口监视器**，波特率选 **115200**。正常启动日志：

```
System start...
LVGL Init complete.
WiFi: connecting to YOUR_WIFI_SSID ...
WiFi: connected, IP=192.168.1.123
Boot finished.
```

记下那个 IP，PC 端要用。

---

## 6. 方式二：arduino-cli 命令行（推荐）

### 6.1 工作区准备

arduino-cli 有两种处理方式，按需选：

```powershell
cd <项目根目录>
```

#### A. 先复制成规范 sketch（推荐一次）

arduino-cli 要求 sketch 文件夹名与 `.ino` 文件名一致。本仓库 `firmware/ESP32_SCREEN.ino` 位于名为 `firmware` 的文件夹，不满足这个规则，所以直接 `compile firmware` 会报错。复制一份到规范目录：

```powershell
mkdir work\ESP32_SCREEN
xcopy /E /I /Y firmware work\ESP32_SCREEN
```

`xcopy /E` 会把 `partitions.csv` 一起拷到 sketch 目录根部，位置正好符合要求。拷完确认一下：

```powershell
dir work\ESP32_SCREEN
# 应该看到 ESP32_SCREEN.ino、config.h、gui.cpp、partitions.csv 等
```

后续编译都针对 `work\ESP32_SCREEN`。

#### B. 就地编译（仓库骨架已按规范命名的情况）

如果文件夹名就是 `ESP32_SCREEN`，可直接编译。

### 6.2 编译

编译命令（**已含自定义分区的上限覆盖**，照抄即可）：

```powershell
arduino-cli compile `
  --fqbn "esp32:esp32:esp32s3:PSRAM=opi,FlashMode=qio,FlashSize=8M,PartitionScheme=min_spiffs,CDCOnBoot=default" `
  --build-property "upload.maximum_size=3932160" `
  work\ESP32_SCREEN
```

成功时输出类似：

```
Sketch uses 1655057 bytes (42%) of program storage space. Maximum is 3932160 bytes.
Global variables use 148216 bytes (45%) of dynamic memory, leaving 179464 bytes for local variables.
```

`42%` 的分母是 3.75MB 自定义 app 槽。如果显示 `84%` 且分母是 1966080，说明 `--build-property` 那行漏了。

参数逐项解释：

| 参数 | 值 | 含义 |
|---|---|---|
| `PSRAM` | `opi` | 使用 OPI PSRAM。N8R8 芯片是八线 PSRAM，选错会导致帧缓冲分配失败、白屏 |
| `FlashMode` | `qio` | 4 线 Flash，8MB 芯片标准配置 |
| `FlashSize` | `8M` | |
| `PartitionScheme` | `min_spiffs` | 只影响编译期上限，实际布局由 sketch 目录的 `partitions.csv` 覆盖（见 5.2） |
| `CDCOnBoot` | `default` | 对应 IDE 的 **USB CDC On Boot: Disabled** |

编译成功后固件在：

```
work\ESP32_SCREEN\build\ESP32_SCREEN.ino.bin
```

想指定输出目录（推荐，方便后面取文件做 OTA）：

```powershell
arduino-cli compile `
  --fqbn "esp32:esp32:esp32s3:PSRAM=opi,FlashMode=qio,FlashSize=8M,PartitionScheme=min_spiffs,CDCOnBoot=default" `
  --build-property "upload.maximum_size=3932160" `
  --build-path "%TEMP%\espbuild" `
  --output-dir "out" `
  work\ESP32_SCREEN
```

这时产物在 `out\ESP32_SCREEN.ino.bin`。

### 6.3 编译速度

字库是纯数据（`FontCN.c` 单文件 3.3MB），首次编译 3~5 分钟。之后**只改动 `gui.cpp`** 这类文件时，增量编译通常 20~40 秒。**不要删 `--build-path` 目录**，否则每次都全量重编。

### 6.4 直接上传

```powershell
arduino-cli upload `
  -p COM5 `
  --fqbn "esp32:esp32:esp32s3:PSRAM=opi,FlashMode=qio,FlashSize=8M,PartitionScheme=min_spiffs,CDCOnBoot=default" `
  --input-dir "out" `
  work\ESP32_SCREEN
```

`-p COM5` 换成你的端口。端口号可以用下面命令查：

```powershell
arduino-cli board list
```

---

## 7. 首次烧录（USB 串口）

已经用 IDE 或 arduino-cli 烧过一遍的话，这一步可以跳过。

如果你拿到的是**别人编译好的 `.bin`**，或者设备卡在某个坏固件上，可以手动烧录。ESP32-S3 的出厂/回刷需要四个部分：

| 偏移 | 文件 |
|---|---|
| `0x0` | bootloader |
| `0x8000` | 分区表 |
| `0x20000` | 应用程序（app0） |

### 7.1 用 esptool 一次烧录

先用上面的编译步骤产出 `ESP32_SCREEN.ino.bootloader.bin`、`.partitions.bin`、`.bin`，然后：

```powershell
esptool --chip esp32s3 -p COM5 -b 921600 write_flash `
  0x0     ESP32_SCREEN.ino.bootloader.bin `
  0x8000  ESP32_SCREEN.ino.partitions.bin `
  0x20000 ESP32_SCREEN.ino.bin
```

> `esptool` 来自 ESP32 核心包，装好板子支持后就有。PATH 里找不到的话，可以用绝对路径：`C:\Users\<用户名>\AppData\Local\Arduino15\packages\esp32\tools\esptool_py\4.5.1\esptool.exe`。

### 7.2 合并成单个镜像（方便分发）

把三部分合成一个 `.bin`，别人拿到只烧 `0x0`：

```powershell
esptool --chip esp32s3 merge_bin -o esp32_screen_full.bin `
  --flash_mode dio --flash_freq 80m --flash_size 8MB `
  0x0     ESP32_SCREEN.ino.bootloader.bin `
  0x8000  ESP32_SCREEN.ino.partitions.bin `
  0x20000 ESP32_SCREEN.ino.bin
```

之后烧录：

```powershell
esptool --chip esp32s3 -p COM5 -b 921600 write_flash 0x0 esp32_screen_full.bin
```

### 7.3 进入下载模式

大多数 ESP32-S3 开发板**插上就能烧**（板载自动复位电路）。如果 esptool 报 `Failed to connect`：

1. 按住 **BOOT** 键不放
2. 按一下 **RST**（或拔插一次 USB）
3. 松开 BOOT
4. 重新执行烧录命令

---

## 8. 后续升级（局域网 OTA）

**烧过带 OTA 固件之后，就不要再拔线烧录了**，直接走网络推送。

### 8.1 前提

- 屏幕和电脑在同一局域网
- 屏幕已经烧入支持 OTA 的固件（本项目所有版本都带）
- 电脑能 ping 通屏幕 IP

### 8.2 推送

启动 PC 端：

```powershell
cd firmware
pip install -r requirements.txt
python pc_agent.py
```

浏览器打开 <http://localhost:5000>，在 OTA 区域选择刚编译出的 `ESP32_SCREEN.ino.bin`，点上传。

屏幕会显示进度条，完成后自动重启。约 1.65MB 的文件在局域网内通常 20~40 秒。

### 8.3 注意事项

- **只推 app 部分**（`ESP32_SCREEN.ino.bin`），不要推 bootloader 或合并镜像
- OTA 会写入备用分区，保留旧版本；如果新固件起不来，可以回滚
- **升级时屏幕会花屏闪烁**，这是已知的硬件限制（帧缓冲在 PSRAM，写 Flash 时共用总线），升级结束后自动正常

---

## 9. 排错

| 现象 | 原因 | 解决 |
|---|---|---|
| 编译报 `ArduinoJson` / `DynamicJsonDocument` 找不到 | 装了 ArduinoJson 7.x | 降级到 6.21.5 |
| 编译报 `lv_...` 一堆未定义 | 装了 lvgl 9.x | 降级到 8.4.0 |
| 编译报 `Arduino_RGB_Display` 参数不匹配 | Arduino_GFX 版本不对 | 用 1.4.7 |
| `lv_conf.h` 找不到 / `LV_CONF_INCLUDE_SIMPLE` 未定义 | `lv_conf.h` 没放在库目录顶层 | 见 [第 3 节](#3-配置-lvgllv_confh) |
| 屏幕白屏 / 花屏 | PSRAM 没选 OPI；或帧缓冲分配失败 | 确认 **PSRAM: OPI PSRAM** |
| **点下拉菜单卡死** | `LV_MEM_SIZE` 仍是 48KB | 改成 96KB（见 3.3） |
| 数字显示成方块或 `f` | 误开了 `LV_SPRINTF_USE_FLOAT` | 保持为 0，数值用整数格式 |
| 中文显示成方块 | 字库不含该汉字 | 用 `lv_font_conv` 重新生成字库，见 README「字体与图标」 |
| WiFi 连不上 | SSID/密码错；或用了 5GHz | 改 `config.h`；确认是 2.4GHz |
| 屏幕显示「等待 PC 数据」 | PC 端没启动或 IP 填错 | 启动 `pc_agent.py`，在网页里填屏的 IP |
| 上传报 `Failed to connect` | 处于运行模式 | 按住 BOOT 再按 RST 进入下载模式 |
| 上传报 `Access denied` / 端口被占用 | 串口监视器还开着 | 关掉串口监视器再上传 |
| 编译报 `Region ... overflowed` 或 `text section exceeds available space in board` | 用了菜单默认上限（1.87MB） | 加 `--build-property "upload.maximum_size=3932160"`，并确认 `partitions.csv` 在 sketch 目录 |
| 编译显示 84%、分母是 1966080 | 上限覆盖没生效 | 检查 `--build-property` 参数是否漏写 |
| 烧录后日志乱码 | 波特率不对 | 串口监视器设为 115200 |

---

## 附录：分区表说明

`firmware/partitions.csv`：

```
# Name,   Type, SubType, Offset,   Size
nvs,      data, nvs,     0x9000,   0x5000
otadata,  data, ota,     0xe000,   0x2000
phy_init, data, phy,     0x10000,  0x1000
app0,     app,  ota_0,   0x20000,  0x3C0000
app1,     app,  ota_1,   0x3E0000, 0x3C0000
coredump, data, coredump,0x7A0000, 0x10000
```

| 分区 | 作用 |
|---|---|
| `nvs` | WiFi 凭据、用户配置等键值存储 |
| `otadata` | 记录当前从 app0 还是 app1 启动，以及升级状态 |
| `phy_init` | 射频校准数据 |
| `app0` / `app1` | 两个 3.75MB 的应用槽，OTA 时轮流写入 |
| `coredump` | 崩溃转储，崩溃后可以用工具解析 |

关键点：

- **两个 app 槽大小必须相同**，OTA 才能双向切换
- 双槽占用 7.5MB，8MB Flash 剩下约 0.5MB 做 NVS/otadata/coredump
- 用 `min_spiffs` 这类内置方案时 app 只有约 1.87MB，装不下将来的功能扩展

### 自定义分区表怎么生效

ESP32 核心 2.0.17 的 `platform.txt` 里有三个预编译钩子，按优先级找分区表：

1. `{sketch目录}\partitions.csv`（最高优先级）
2. 变体目录下的 `partitions.csv`
3. 核心包内置的 `tools/partitions/{方案名}.csv`（由 Partition Scheme 菜单决定）

所以只要把 `partitions.csv` 放在 sketch 根目录就会生效。但这个文件只改变**实际烧录的分区布局**，编译期的 `upload.maximum_size` 仍然来自菜单项，需要另外覆盖：

```powershell
--build-property "upload.maximum_size=3932160"
```

> 3932160 = `0x3C0000`，即我们的 app 槽大小。

**这一组合只在首次烧录时重要**：

- 首次用 USB 烧入自定义分区表后，之后所有 OTA 都按新布局工作
- 只要不动分区表，后续日常编译不加 `--build-property` 也能过（因为 1.58MB < 1.87MB），但一旦功能增加到超过 1.87MB 就必须加

换了分区表之后**必须重新 USB 烧录一次**（因为分区表本身在 Flash 的固定位置），之后才能继续用 OTA。

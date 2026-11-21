#!/usr/bin/env python3
"""
ESP32 桌面信息副屏 —— PC 端代理

功能：
  - Flask 网页配置（屏 IP、天气城市/Key、刷新间隔、GPU 开关）
  - 后台线程：采集 CPU/内存/GPU + 抓天气/热搜，每 N 秒 POST 给屏
  - 网页 /status 查看推送状态

依赖：
  pip install flask requests psutil pynvml
  (pynvml 仅 NVIDIA GPU 需要；无 GPU 可在网页关闭)

运行：
  python pc_agent.py
  浏览器打开 http://localhost:5000 配置
"""

import json
import os
import threading
import time

import requests
import psutil
from flask import Flask, request, redirect, render_template_string, jsonify

try:
    import pynvml
    HAS_NVML = True
except ImportError:
    HAS_NVML = False

CONFIG_PATH = os.path.join(os.path.dirname(os.path.abspath(__file__)), "config.json")

DEFAULT_CONFIG = {
    "screen_ip": "192.168.1.100",
    "push_interval": 2,        # 秒
    "qweather_key": "",        # 和风天气 API Key
    "qweather_city": "101010100",  # 和风 LocationID（北京）
    "qweather_host": "devapi.qweather.com",
    "enable_gpu": True,
    "enable_hot": True,
    "slot2": "mem",            # 第二指标: mem / disk / gpu / cpu
    "slot3": "disk",           # 第三指标: mem / disk / gpu / cpu
}

# 热搜/天气缓存（变化慢，定期刷新，避免每次都请求）
CACHE = {
    "weather": {"temp": 0, "desc": "--", "code": "na"},
    "hot": [],
    "last_weather_fetch": 0,
    "last_hot_fetch": 0,
    "last_push_ok": False,
    "last_push_time": 0,
    "last_error": "",
}

WEATHER_REFRESH_SEC = 600   # 天气每 10 分钟
HOT_REFRESH_SEC = 900       # 热搜每 15 分钟

app = Flask(__name__)


def load_config():
    if os.path.exists(CONFIG_PATH):
        try:
            with open(CONFIG_PATH, "r", encoding="utf-8") as f:
                cfg = json.load(f)
            return {**DEFAULT_CONFIG, **cfg}
        except Exception:
            pass
    return dict(DEFAULT_CONFIG)


def save_config(cfg):
    with open(CONFIG_PATH, "w", encoding="utf-8") as f:
        json.dump(cfg, f, ensure_ascii=False, indent=2)


config = load_config()

# 工时监控（LogonUI 轮询）
try:
    from work_monitor import WorkMonitor
    _work = WorkMonitor()
    _work_thread_started = False
    def _work_loop():
        while True:
            try:
                _work.tick()
            except Exception as e:
                print("[work]", e)
            time.sleep(3)
    threading.Thread(target=_work_loop, daemon=True).start()
    print("[work] 工时监控已启动")
except Exception as e:
    print("[work] 工时监控未启用:", e)
    _work = None

# ───────────────────────── 采集 ─────────────────────────

def read_cpu_temp():
    """尽量读 CPU 温度，读不到返回 0。仅 Linux 的 /sys 或 psutil sensors。"""
    try:
        temps = psutil.sensors_temperatures()
        if temps:
            for name in ("coretemp", "k10temp", "cpu_thermal", "acpitz"):
                if name in temps and temps[name]:
                    return round(temps[name][0].current, 1)
    except Exception:
        pass
    return 0.0


def read_cpu_freq():
    """CPU 当前频率 MHz，读不到返回 0。"""
    try:
        f = psutil.cpu_freq()
        return int(f.current) if f else 0
    except Exception:
        return 0


# 网速：保存上次采样
_net_last = {"time": 0, "sent": 0, "recv": 0}
# 磁盘读写：保存上次采样
_disk_io_last = {"time": 0, "read": 0, "write": 0}


def read_disk_io():
    """返回 (读 KB/s, 写 KB/s)。两盘合计。"""
    global _disk_io_last
    try:
        io = psutil.disk_io_counters()  # 所有盘合计
        now = time.time()
        rd = wr = 0.0
        if _disk_io_last["time"] > 0:
            dt = now - _disk_io_last["time"]
            if dt > 0:
                rd = max(0, (io.read_bytes - _disk_io_last["read"]) / dt / 1024)
                wr = max(0, (io.write_bytes - _disk_io_last["write"]) / dt / 1024)
        _disk_io_last = {"time": now, "read": io.read_bytes, "write": io.write_bytes}
        return round(rd, 1), round(wr, 1)
    except Exception:
        return 0.0, 0.0


def read_net_speed():
    """返回 (下行 KB/s, 上行 KB/s)。基于两次采样差值。"""
    global _net_last
    try:
        io = psutil.net_io_counters()
        now = time.time()
        down = up = 0
        if _net_last["time"] > 0:
            dt = now - _net_last["time"]
            if dt > 0:
                down = max(0, (io.bytes_recv - _net_last["recv"]) / dt / 1024)
                up = max(0, (io.bytes_sent - _net_last["sent"]) / dt / 1024)
        _net_last = {"time": now, "sent": io.bytes_sent, "recv": io.bytes_recv}
        return round(down, 1), round(up, 1)
    except Exception:
        return 0.0, 0.0


def read_gpu():
    """返回 (util%, temp℃)。无 NVML 或失败返回 (0, 0)。"""
    if not (HAS_NVML and config.get("enable_gpu")):
        return 0.0, 0.0
    try:
        pynvml.nvmlInit()
        handle = pynvml.nvmlDeviceGetHandleByIndex(0)
        util = pynvml.nvmlDeviceGetUtilizationRates(handle).gpu
        temp = pynvml.nvmlDeviceGetTemperature(handle, pynvml.NVML_TEMPERATURE_GPU)
        return round(util, 0), round(temp, 0)
    except Exception:
        return 0.0, 0.0


def read_gpu_detail():
    """返回 (显存已用MB, 显存总MB, 功耗W)。失败返回 (0,0,0)。"""
    if not (HAS_NVML and config.get("enable_gpu")):
        return 0, 0, 0
    try:
        handle = pynvml.nvmlDeviceGetHandleByIndex(0)
        mem = pynvml.nvmlDeviceGetMemoryInfo(handle)
        try:
            power = pynvml.nvmlDeviceGetPowerUsage(handle) / 1000.0  # mW -> W
        except Exception:
            power = 0
        return int(mem.used / 1048576), int(mem.total / 1048576), round(power, 0)
    except Exception:
        return 0, 0, 0


def fetch_weather():
    """和风天气实时天气。失败则保留旧值。"""
    key = config.get("qweather_key", "").strip()
    if not key:
        return
    try:
        url = f"https://{config['qweather_host']}/v7/weather/now"
        params = {"location": config["qweather_city"], "key": key}
        r = requests.get(url, params=params, timeout=5)
        data = r.json()
        if data.get("code") == "200":
            now = data["now"]
            CACHE["weather"] = {
                "temp": int(now["temp"]),
                "desc": now["text"],
                "code": now.get("icon", "na"),   # 和风 icon 码，屏端也可直接用 desc
            }
    except Exception as e:
        print(f"[weather] {e}")


def fetch_hotlist():
    """微博热搜。免登录 JSON 接口。"""
    if not config.get("enable_hot"):
        return
    try:
        url = "https://weibo.com/ajax/side/hotSearch"
        headers = {
            "User-Agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64)",
            "Referer": "https://weibo.com/",
        }
        r = requests.get(url, headers=headers, timeout=5)
        data = r.json()
        realtime = data.get("data", {}).get("realtime", [])
        hot = [item.get("word", "") for item in realtime if item.get("word")]
        if hot:
            CACHE["hot"] = hot[:10]
    except Exception as e:
        print(f"[hot] {e}")


def build_packet():
    now = time.time()
    if now - CACHE["last_weather_fetch"] > WEATHER_REFRESH_SEC:
        threading.Thread(target=fetch_weather, daemon=True).start()
        CACHE["last_weather_fetch"] = now
    if now - CACHE["last_hot_fetch"] > HOT_REFRESH_SEC:
        threading.Thread(target=fetch_hotlist, daemon=True).start()
        CACHE["last_hot_fetch"] = now

    gpu_util, gpu_temp = read_gpu()
    gpu_mem_used, gpu_mem_total, gpu_power = read_gpu_detail()
    net_down, net_up = read_net_speed()
    disk_read, disk_write = read_disk_io()
    lt = time.localtime()

    # 内存 GB
    vm = psutil.virtual_memory()
    mem_used_gb = round(vm.used / (1024**3), 1)
    mem_total_gb = round(vm.total / (1024**3), 1)

    # 磁盘：系统盘
    disk_pct, disk_used_gb, disk_total_gb = 0, 0, 0
    try:
        drive = (os.environ.get("SystemDrive", "C:") + "\\") if os.name == "nt" else "/"
        du = psutil.disk_usage(drive)
        disk_pct = round(du.percent, 0)
        disk_used_gb = round(du.used / (1024**3), 0)
        disk_total_gb = round(du.total / (1024**3), 0)
    except Exception:
        pass

    return {
        "cpu": round(psutil.cpu_percent(interval=None), 0),
        "cpu_temp": read_cpu_temp(),
        "cpu_freq": read_cpu_freq(),
        "gpu": gpu_util,
        "gpu_temp": gpu_temp,
        "gpu_mem_used": gpu_mem_used,
        "gpu_mem_total": gpu_mem_total,
        "gpu_power": gpu_power,
        "mem": round(vm.percent, 0),
        "mem_used_gb": mem_used_gb,
        "mem_total_gb": mem_total_gb,
        "disk": disk_pct,
        "disk_used_gb": int(disk_used_gb),
        "disk_total_gb": int(disk_total_gb),
        "disk_read": disk_read,     # KB/s
        "disk_write": disk_write,   # KB/s
        # 磁盘活跃度：读写速度归一化 (参考 200MB/s 满量程)，0-100
        "disk_active": min(100, int((disk_read + disk_write) / 1024 / 200 * 100)),
        "net_down": net_down,   # KB/s
        "net_up": net_up,       # KB/s
        "slot2": config.get("slot2", "mem"),
        "slot3": config.get("slot3", "disk"),
        "host": os.environ.get("COMPUTERNAME", "PC"),
        "time": time.strftime("%H:%M", lt),
        "date": time.strftime("%m-%d", lt),
        "year": lt.tm_year,
        "month": lt.tm_mon,
        "day": lt.tm_mday,
        "weekday": lt.tm_wday,  # 0=周一
        "weather": CACHE["weather"],
        "hot": CACHE["hot"],
        "work": _work_packet(),
    }


def _work_packet():
    if _work is None:
        return {"active": False, "start": "--:--", "end": "--:--", "minutes": 0,
                "hist": [], "hist_date": []}
    t = _work.get_today()
    hist = _work.get_history(14)
    return {
        "active": t["active"],
        "start": t["start"],
        "end": t["end"],
        "minutes": t["minutes"],
        "hist": [m for _, m in hist],
        "hist_date": [d for d, _ in hist],
    }


def push_loop():
    psutil.cpu_percent(interval=None)  # 预热
    while True:
        try:
            pkt = build_packet()
            url = f"http://{config['screen_ip']}/api/pc"
            r = requests.post(url, json=pkt, timeout=3)
            CACHE["last_push_ok"] = (r.status_code == 200)
            CACHE["last_push_time"] = time.time()
            if r.status_code != 200:
                CACHE["last_error"] = f"HTTP {r.status_code}"
        except Exception as e:
            CACHE["last_push_ok"] = False
            CACHE["last_error"] = str(e)
        time.sleep(max(1, config.get("push_interval", 2)))


# ───────────────────────── 网页 ─────────────────────────

PAGE = """
<!doctype html><html lang=zh><head><meta charset=utf-8>
<title>副屏配置</title>
<meta name=viewport content="width=device-width,initial-scale=1">
<style>
body{font-family:system-ui,-apple-system,sans-serif;max-width:520px;margin:40px auto;padding:0 16px;background:#111;color:#ddd}
h1{font-size:20px}label{display:block;margin:14px 0 4px;font-size:14px;color:#9ab}
input[type=text],input[type=number]{width:100%;padding:8px;box-sizing:border-box;background:#1e1e1e;border:1px solid #333;color:#eee;border-radius:6px}
.row{display:flex;gap:12px}.row>div{flex:1}
button{margin-top:20px;padding:10px 18px;background:#00a060;border:0;color:#fff;border-radius:6px;font-size:15px;cursor:pointer}
.status{margin-top:24px;padding:12px;border-radius:6px;background:#1a1a1a;font-size:13px;line-height:1.7}
.ok{color:#3c3}.bad{color:#e55}
</style></head><body>
<h1>ESP32 桌面信息副屏 · PC 端配置</h1>
<form method=post action=/save>
  <label>屏 IP 地址</label>
  <input type=text name=screen_ip value="{{c.screen_ip}}">
  <div class=row>
    <div><label>推送间隔（秒）</label><input type=number name=push_interval value="{{c.push_interval}}"></div>
    <div><label>GPU 采集</label>
      <select name=enable_gpu style="width:100%;padding:8px;background:#1e1e1e;border:1px solid #333;color:#eee;border-radius:6px">
        <option value=1 {{'selected' if c.enable_gpu}}>开</option>
        <option value=0 {{'selected' if not c.enable_gpu}}>关</option>
      </select></div>
  </div>
  <div class=row>
    <div><label>指标卡 2</label>
      <select name=slot2 style="width:100%;padding:8px;background:#1e1e1e;border:1px solid #333;color:#eee;border-radius:6px">
        {% for v,n in [('mem','内存占用'),('disk','磁盘占用'),('gpu','GPU占用'),('cpu','CPU占用')] %}
        <option value={{v}} {{'selected' if c.slot2==v}}>{{n}}</option>{% endfor %}
      </select></div>
    <div><label>指标卡 3</label>
      <select name=slot3 style="width:100%;padding:8px;background:#1e1e1e;border:1px solid #333;color:#eee;border-radius:6px">
        {% for v,n in [('mem','内存占用'),('disk','磁盘占用'),('gpu','GPU占用'),('cpu','CPU占用')] %}
        <option value={{v}} {{'selected' if c.slot3==v}}>{{n}}</option>{% endfor %}
      </select></div>
  </div>
  <label>和风天气 API Key</label>
  <input type=text name=qweather_key value="{{c.qweather_key}}" placeholder="留空则不显示天气">
  <label>和风 LocationID（城市）</label>
  <input type=text name=qweather_city value="{{c.qweather_city}}">
  <div class=row>
    <div><label>和风 Host</label><input type=text name=qweather_host value="{{c.qweather_host}}"></div>
    <div><label>热搜</label>
      <select name=enable_hot style="width:100%;padding:8px;background:#1e1e1e;border:1px solid #333;color:#eee;border-radius:6px">
        <option value=1 {{'selected' if c.enable_hot}}>开</option>
        <option value=0 {{'selected' if not c.enable_hot}}>关</option>
      </select></div>
  </div>
  <button>保存并生效</button>
</form>

<h2 style="font-size:16px;margin-top:32px">固件升级 (OTA)</h2>
<form id=otaForm>
  <label>选择固件 .bin 文件（app 分区镜像，即 xxx.ino.bin）</label>
  <input type=file name=fw accept=".bin" id=fwFile>
  <button type=button onclick=doOta()>上传到屏并升级</button>
</form>
<div class=status id=otaStatus></div>
<script>
async function doOta(){
  const f=document.getElementById('fwFile').files[0];
  const st=document.getElementById('otaStatus');
  if(!f){st.textContent='请先选择 .bin 文件';return;}
  st.textContent='上传中…（约 1MB，请稍候）';
  const fd=new FormData(); fd.append('fw', f);
  try{
    const r=await fetch('/ota',{method:'POST',body:fd});
    const j=await r.json();
    st.innerHTML = j.ok ? '<span class=ok>升级成功，屏正在重启…</span>' : '<span class=bad>失败：'+(j.err||'')+'</span>';
  }catch(e){ st.innerHTML='<span class=bad>请求出错：'+e+'</span>'; }
}
</script>
<div class=status>{{status_html|safe}}</div>
</body></html>
"""


def status_html():
    ok = CACHE["last_push_ok"]
    cls = "ok" if ok else "bad"
    txt = "在线" if ok else "未连通"
    ago = int(time.time() - CACHE["last_push_time"]) if CACHE["last_push_time"] else -1
    err = CACHE.get("last_error", "")
    return (f'推送状态：<span class="{cls}">{txt}</span><br>'
            f'上次推送：{ago}s 前<br>'
            f'天气：{CACHE["weather"]["desc"]} {CACHE["weather"]["temp"]}℃<br>'
            f'热搜：{len(CACHE["hot"])} 条<br>'
            f'错误：{err}')


@app.route("/")
def index():
    return render_template_string(PAGE, c=config, status_html=status_html())


@app.route("/save", methods=["POST"])
def save():
    global config
    config = {
        "screen_ip": request.form.get("screen_ip", config["screen_ip"]).strip(),
        "push_interval": int(request.form.get("push_interval", 2) or 2),
        "qweather_key": request.form.get("qweather_key", "").strip(),
        "qweather_city": request.form.get("qweather_city", "").strip(),
        "qweather_host": request.form.get("qweather_host", "devapi.qweather.com").strip(),
        "enable_gpu": request.form.get("enable_gpu") == "1",
        "enable_hot": request.form.get("enable_hot") == "1",
        "slot2": request.form.get("slot2", "mem"),
        "slot3": request.form.get("slot3", "disk"),
    }
    save_config(config)
    return redirect("/")


@app.route("/status")
def status():
    return jsonify(CACHE)


@app.route("/ota", methods=["POST"])
def ota():
    f = request.files.get("fw")
    if not f:
        return jsonify({"ok": False, "err": "no file"})
    data = f.read()
    if len(data) < 10000:
        return jsonify({"ok": False, "err": "file too small"})
    try:
        url = f"http://{config['screen_ip']}/api/ota"
        r = requests.post(url, files={"firmware": ("fw.bin", data, "application/octet-stream")}, timeout=60)
        try:
            j = r.json()
        except Exception:
            j = {"ok": r.status_code == 200}
        return jsonify({"ok": bool(j.get("ok")), "err": j.get("err", f"HTTP {r.status_code}")})
    except Exception as e:
        return jsonify({"ok": False, "err": str(e)})


if __name__ == "__main__":
    print("PC 端代理启动，浏览器打开 http://localhost:5000 配置")
    threading.Thread(target=push_loop, daemon=True).start()
    app.run(host="0.0.0.0", port=5000)

"""
工时监控模块 —— 通过检测 Windows LogonUI.exe 判断锁屏/解锁

规则：
  - 当天首次解锁 = 上班时间（work_start）
  - 锁屏后若持续 >= 2 小时无解锁 = 判定下班，下班点 = 那次锁屏时间
  - 短锁屏（<2h）不算下班，继续上班中
  - 跨天工时归开始日
  - 周末照常记录

持久化 work_log.json：{ "YYYY-MM-DD": {"start":"09:00","end":"18:30","minutes":570,"locked_at":null}, ... }
"""

import json
import os
import time
import datetime
import psutil

LOG_PATH = os.path.join(os.path.dirname(os.path.abspath(__file__)), "work_log.json")
LOCK_GAP_SEC = 2 * 60 * 60      # 2 小时判定下班
POLL_SEC = 3                     # 轮询间隔

# 运行态
_last_locked = None              # 是否锁屏（None=初始）
_lock_since = None               # 本次锁屏开始时间(datetime)
_today = None


def _now():
    return datetime.datetime.now()


def _is_locked():
    """检测 LogonUI.exe 是否存在 = 锁屏中"""
    try:
        for p in psutil.process_iter(["name"]):
            n = p.info.get("name") or ""
            if n.lower() == "logonui.exe":
                return True
    except Exception:
        pass
    return False


def _load():
    if os.path.exists(LOG_PATH):
        try:
            with open(LOG_PATH, "r", encoding="utf-8") as f:
                return json.load(f)
        except Exception:
            pass
    return {}


def _save(data):
    try:
        with open(LOG_PATH, "w", encoding="utf-8") as f:
            json.dump(data, f, ensure_ascii=False, indent=2)
    except Exception as e:
        print("[work] save fail:", e)


class WorkMonitor:
    def __init__(self):
        self.data = _load()
        self.today = _now().date()
        self.locked = _is_locked()
        self.lock_since = _now() if self.locked else None
        # 确保今天有记录结构
        self._ensure_today()
        # 尝试用事件日志拿当天首次解锁时间（最准，需管理员）
        self._try_eventlog_start()

    def _try_eventlog_start(self):
        rec = self.data[self._key()]
        if rec.get("start"):     # 已有上班时间（今天已记录过）不覆盖
            return
        try:
            from eventlog_work import get_today_unlock_time
            t = get_today_unlock_time()
            if t:
                rec["start"] = t
                print("[work] 从事件日志得到今天首次解锁:", t)
                self.save()
        except Exception as e:
            print("[work] 事件日志读取失败(需管理员):", e)

    def _key(self, d=None):
        return (d or self.today).strftime("%Y-%m-%d")

    def _ensure_today(self):
        k = self._key()
        if k not in self.data:
            self.data[k] = {"start": None, "end": None, "minutes": 0, "locked_at": None}

    def _recalc_minutes(self):
        """根据 start/end 重算今日分钟"""
        rec = self.data[self._key()]
        if rec["start"] and rec["end"]:
            s = datetime.datetime.strptime(rec["start"], "%H:%M")
            e = datetime.datetime.strptime(rec["end"], "%H:%M")
            mins = int((e - s).total_seconds() / 60)
            rec["minutes"] = max(0, mins)

    def tick(self):
        """主循环调用，检测状态变化"""
        now = _now()
        today = now.date()

        # 跨天检查
        if today != self.today:
            self.today = today
            self._ensure_today()
            self.lock_since = now if self.locked else None
            self._try_eventlog_start()   # 新的一天，读当天首解

        locked_now = _is_locked()
        rec = self.data[self._key()]

        # 解锁（锁屏 -> 未锁屏）
        if self.locked and not locked_now:
            gap = (now - self.lock_since).total_seconds() if self.lock_since else 0
            if gap >= LOCK_GAP_SEC and rec["start"] and not rec["end"]:
                # 距锁屏已超 2h -> 那次锁屏即下班；当天工时锁定
                rec["end"] = self.lock_since.strftime("%H:%M")
                rec["locked_at"] = None
                self._recalc_minutes()
                self.save()
            self.lock_since = None

        # 锁屏（未锁 -> 锁屏）
        if not self.locked and locked_now:
            self.lock_since = now
            if not rec["end"]:                 # 当天还没下班才记 locked_at
                rec["locked_at"] = now.strftime("%H:%M")

        self.locked = locked_now

        # 首次上班：已下班前、未锁屏、今天还没 start -> 记上班
        if not locked_now and not rec["end"] and rec["start"] is None:
            rec["start"] = now.strftime("%H:%M")
            rec["minutes"] = 0
            self.save()

    def get_today(self):
        """返回今日工时状态（上班中实时计算分钟）"""
        rec = self.data[self._key()]
        minutes = rec["minutes"]
        # 上班中（有 start 无 end）：实时 = now - start
        if rec["start"] and not rec["end"]:
            try:
                now = _now()
                s = datetime.datetime.combine(self.today, datetime.datetime.strptime(rec["start"], "%H:%M").time())
                minutes = int((now - s).total_seconds() / 60)
                minutes = max(0, minutes)
            except Exception:
                minutes = rec["minutes"]
        return {
            "active": rec["start"] is not None,
            "start": rec["start"] or "--:--",
            "end": rec["end"] or "--:--",
            "minutes": minutes,
        }

    def get_history(self, days=14):
        """返回最近 days 天：[(date_str, minutes), ...] 从早到晚"""
        out = []
        today = _now().date()
        today_rec = self.get_today()   # 今天用实时分钟
        for i in range(days - 1, -1, -1):
            d = today - datetime.timedelta(days=i)
            if i == 0:
                mins = today_rec["minutes"]
            else:
                k = d.strftime("%Y-%m-%d")
                rec = self.data.get(k)
                mins = rec["minutes"] if rec else 0
            out.append((d.strftime("%m/%d"), mins))
        return out

    def save(self):
        _save(self.data)

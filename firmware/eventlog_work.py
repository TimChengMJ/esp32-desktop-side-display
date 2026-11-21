"""
从 Windows 安全事件日志读取当天的锁屏/解锁事件，得到当天首次解锁时间。
需要管理员权限（Security 日志）。

事件 ID：
  4800 = 工作站已锁定
  4801 = 工作站已解锁
"""

import datetime
import win32evtlog


def get_today_unlock_time():
    """
    返回今天的首次解锁时间字符串 "HH:MM"，读不到返回 None。
    需要管理员权限。
    """
    today = datetime.date.today()
    day_start = datetime.datetime.combine(today, datetime.time.min)

    try:
        h = win32evtlog.OpenEventLog(None, "Security")
    except Exception as e:
        print("[eventlog] 打不开 Security 日志(需管理员):", e)
        return None

    flags = win32evtlog.EVENTLOG_BACKWARDS_READ | win32evtlog.EVENTLOG_SEQUENTIAL_READ
    first_unlock = None
    scanned = 0
    try:
        while True:
            records = win32evtlog.ReadEventLog(h, flags, 0)
            if not records:
                break
            for rec in records:
                scanned += 1
                gen = rec.TimeGenerated  # datetime
                if gen < day_start:
                    # 已经翻到昨天及更早，停止
                    win32evtlog.CloseEventLog(h)
                    return first_unlock.strftime("%H:%M") if first_unlock else None
                if rec.EventID == 4801:  # 解锁
                    first_unlock = gen  # 反向读，越往后越早，持续覆盖 -> 最终是当天最早
            if scanned > 20000:
                break
    except Exception as e:
        print("[eventlog] 读取异常:", e)
    finally:
        try:
            win32evtlog.CloseEventLog(h)
        except Exception:
            pass

    return first_unlock.strftime("%H:%M") if first_unlock else None


if __name__ == "__main__":
    print("今天首次解锁:", get_today_unlock_time())

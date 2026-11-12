"""Do injected moves wrap? Decisive test for RDP compatibility.

The plugin ignores every move carrying LLMHF_INJECTED, justified in the source
as "a programmatic SetCursorPos must not cascade". But a measurement showed
SetCursorPos produces flags=0x0 (NOT injected), so that justification is false:
the anti-oscillation guard is what stops the cascade, and the injected filter
is doing something else entirely - dropping every move produced by SendInput.

That matters for Remote Desktop, where the client's physical mouse may be
delivered to the session through the injected-input path: a blanket filter
would ignore every real move and the plugin would look dead.

This probe drives the cursor with SendInput (injected) and asks the plugin's
own counters whether it wrapped. Run it before and after the fix.
"""
from __future__ import annotations

import ctypes
import ctypes.wintypes as wt
import json
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
EXE = ROOT / "cursorwrap.exe"
user32 = ctypes.WinDLL("user32", use_last_error=True)

INPUT_MOUSE = 0
MOUSEEVENTF_MOVE = 0x0001
MOUSEEVENTF_ABSOLUTE = 0x8000


class MOUSEINPUT(ctypes.Structure):
    _fields_ = [("dx", wt.LONG), ("dy", wt.LONG), ("mouseData", wt.DWORD),
                ("dwFlags", wt.DWORD), ("time", wt.DWORD),
                ("dwExtraInfo", ctypes.c_size_t)]


class INPUT(ctypes.Structure):
    class _U(ctypes.Union):
        _fields_ = [("mi", MOUSEINPUT)]
    _anonymous_ = ("u",)
    _fields_ = [("type", wt.DWORD), ("u", _U)]


user32.SendInput.argtypes = [wt.UINT, ctypes.POINTER(INPUT), ctypes.c_int]
user32.SendInput.restype = wt.UINT
user32.SetCursorPos.argtypes = [ctypes.c_int, ctypes.c_int]
user32.GetCursorPos.argtypes = [ctypes.POINTER(wt.POINT)]
user32.GetSystemMetrics.argtypes = [ctypes.c_int]


def send_move(x: int, y: int) -> None:
    sw, sh = user32.GetSystemMetrics(0), user32.GetSystemMetrics(1)
    inp = INPUT()
    inp.type = INPUT_MOUSE
    inp.mi.dx = int(x * 65535 / max(1, sw - 1))
    inp.mi.dy = int(y * 65535 / max(1, sh - 1))
    inp.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE
    user32.SendInput(1, ctypes.byref(inp), ctypes.sizeof(INPUT))


def cursor_pos() -> tuple[int, int]:
    pt = wt.POINT()
    user32.GetCursorPos(ctypes.byref(pt))
    return pt.x, pt.y


class Driver:
    def __init__(self):
        self.proc = subprocess.Popen(
            [str(EXE)], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
            stderr=subprocess.PIPE, text=True, encoding="utf-8", bufsize=1)
        self.next_id = 1

    def call(self, method, params):
        msg_id = self.next_id
        self.next_id += 1
        self.proc.stdin.write(json.dumps(
            {"jsonrpc": "2.0", "id": msg_id, "method": method,
             "params": params}) + "\n")
        self.proc.stdin.flush()
        deadline = time.time() + 10
        while time.time() < deadline:
            line = self.proc.stdout.readline()
            if not line:
                break
            try:
                msg = json.loads(line)
            except json.JSONDecodeError:
                continue
            if msg.get("id") == msg_id:
                return msg
        return {}

    def notify(self, method, params):
        self.proc.stdin.write(json.dumps(
            {"jsonrpc": "2.0", "method": method, "params": params}) + "\n")
        self.proc.stdin.flush()

    def close(self):
        try:
            self.proc.stdin.close()
        except Exception:
            pass
        try:
            return self.proc.wait(timeout=10)
        except subprocess.TimeoutExpired:
            self.proc.kill()
            return -1


def counters(drv) -> dict:
    resp = drv.call("query", {"text": "diag", "raw": "diag",
                              "trigger": "keyword", "keyword": "cursorwrap"})
    items = (resp.get("result") or {}).get("items") or []
    text = items[0].get("copy", "") if items else ""
    out = {"moves": 0, "wraps": 0, "injected": 0}
    for line in text.splitlines():
        for key in ("moves", "wraps"):
            if line.startswith(f"{key}: "):
                out[key] = int(line.split(": ", 1)[1])
        if line.startswith("hook injected: "):
            out["injected"] = int(line.split(": ", 1)[1])
    return out


BASE = {"wrapMode": "both", "activationMode": "always", "disableDuringDrag": True,
        "disableOnSingleMonitor": False, "edgeThresholdPx": 1, "wrapGuardPx": 50,
        "adjacencyTolerancePx": 50, "impactGate": False, "impactSpeedPps": 2000,
        "impactBandPx": 150}


def main() -> int:
    sw, sh = user32.GetSystemMetrics(0), user32.GetSystemMetrics(1)
    mid_y = sh // 2
    print(f"screen {sw}x{sh}")
    with tempfile.TemporaryDirectory(prefix="cw-inj-") as tmp:
        drv = Driver()
        drv.call("initialize", {"protocolVersion": "1", "winshun": "0.22.0",
                                "pluginDir": str(ROOT), "dataDir": tmp,
                                "locale": "en", "settings": BASE})
        drv.notify("settingsChanged", {"settings": BASE})
        time.sleep(0.25)

        for direction, target in (("right", sw - 1), ("left", 0)):
            user32.SetCursorPos(sw // 2, mid_y)
            time.sleep(0.12)
            before = counters(drv)
            sx = sw // 2
            for i in range(1, 17):
                send_move(sx + (target - sx) * i // 16, mid_y)
                time.sleep(0.012)
            time.sleep(0.35)
            after = counters(drv)
            landed = cursor_pos()
            print(f"  injected push toward the {direction:<5}: "
                  f"delta_moves={after['moves'] - before['moves']:<3} "
                  f"delta_wraps={after['wraps'] - before['wraps']:<2} "
                  f"injected_seen={after['injected'] - before['injected']:<3} "
                  f"landed={landed}")

        drv.notify("shutdown", {})
        time.sleep(0.3)
        print("exit:", drv.close())
    print("\nexpected BEFORE the fix: delta_wraps=0 and injected_seen>0")
    print("expected AFTER  the fix: delta_wraps=1 and injected_seen>0")
    return 0


if __name__ == "__main__":
    sys.exit(main())

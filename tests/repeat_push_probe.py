"""Repeat one push N times and report every intermediate position.

Distinguishes a deterministic right-edge bug from interference: a real bug
fails the same way each round at the same place; interference lands at random
coordinates (the earlier evidence showed y drifting 540 -> 1062, which no
programmatic push can cause, because every commanded point keeps y at 540).
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
user32.SetCursorPos.argtypes = [ctypes.c_int, ctypes.c_int]
user32.GetCursorPos.argtypes = [ctypes.POINTER(wt.POINT)]


def pos() -> tuple[int, int]:
    pt = wt.POINT()
    user32.GetCursorPos(ctypes.byref(pt))
    return pt.x, pt.y


def move_to(x, y) -> bool:
    return bool(user32.SetCursorPos(int(x), int(y)))


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


def counts(drv) -> tuple[int, int]:
    resp = drv.call("query", {"text": "diag", "raw": "diag",
                              "trigger": "keyword", "keyword": "cursorwrap"})
    items = (resp.get("result") or {}).get("items") or []
    text = items[0].get("copy", "") if items else ""
    moves = wraps = 0
    for line in text.splitlines():
        if line.startswith("moves: "):
            moves = int(line.split(": ", 1)[1])
        elif line.startswith("wraps: "):
            wraps = int(line.split(": ", 1)[1])
    return moves, wraps


BASE = {"wrapMode": "both", "activationMode": "always", "disableDuringDrag": True,
        "disableOnSingleMonitor": False, "edgeThresholdPx": 1, "wrapGuardPx": 50,
        "adjacencyTolerancePx": 50, "impactGate": False, "impactSpeedPps": 2000,
        "impactBandPx": 150}


def main() -> int:
    rounds = int(sys.argv[1]) if len(sys.argv) > 1 else 4
    sw, sh = user32.GetSystemMetrics(0), user32.GetSystemMetrics(1)
    mid_y = sh // 2
    print(f"screen {sw}x{sh}; {rounds} rounds per direction\n")
    with tempfile.TemporaryDirectory(prefix="cw-rep-") as tmp:
        drv = Driver()
        drv.call("initialize", {"protocolVersion": "1", "winshun": "0.22.0",
                                "pluginDir": str(ROOT), "dataDir": tmp,
                                "locale": "en", "settings": BASE})
        drv.notify("settingsChanged", {"settings": BASE})
        time.sleep(0.25)

        for direction, target_x in (("right", sw - 1), ("left", 0)):
            print(f"=== toward the {direction} edge (target x={target_x}) ===")
            for r in range(rounds):
                move_to(sw // 2, mid_y)
                time.sleep(0.15)
                m0, w0 = counts(drv)
                sx = sw // 2
                trail = []
                for i in range(1, 17):
                    x = sx + (target_x - sx) * i // 16
                    move_to(x, mid_y)
                    time.sleep(0.012)
                    trail.append(pos())
                time.sleep(0.35)
                m1, w1 = counts(drv)
                final = pos()
                # Every commanded point keeps y == mid_y: any other y means an
                # outside force moved the pointer.
                yclean = all(t[1] == mid_y for t in trail)
                print(f"  round {r + 1}: delta_wraps={w1 - w0} delta_moves={m1 - m0} "
                      f"final={final} y_stable={yclean}")
                if not yclean:
                    off = [t for t in trail if t[1] != mid_y]
                    print(f"      y drifted at {len(off)} step(s), e.g. {off[:3]}")
                print(f"      trail tail: {trail[-4:]}")
        drv.notify("shutdown", {})
        time.sleep(0.3)
        print("\nexit:", drv.close())
    return 0


if __name__ == "__main__":
    sys.exit(main())

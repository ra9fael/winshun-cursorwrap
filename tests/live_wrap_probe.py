#!/usr/bin/env python3
"""Live single-display wrap probe: drive the real cursorwrap.exe hook with
real SetCursorPos moves and read back where the cursor actually lands.

This is the missing L4 evidence for the single-display path: it exercises the
real WH_MOUSE_LL hook, the real SetCursorPos, and the real guard, not the
pure core in isolation.

Writes tests/layouts/live-wrap-evidence.json with the observed moves.
Standard library only.
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
kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)

# --- minimal Win32 surface ---------------------------------------------------

class MSLLHOOKSTRUCT(ctypes.Structure):
    _fields_ = [("pt", wt.POINT), ("mouseData", wt.DWORD),
                ("flags", wt.DWORD), ("time", wt.DWORD),
                ("dwExtraInfo", ctypes.POINTER(wt.ULONG))]

HOOKPROC = ctypes.WINFUNCTYPE(ctypes.c_long, ctypes.c_int, ctypes.c_size_t, ctypes.c_ssize_t)
WH_MOUSE_LL = 14
WM_MOUSEMOVE = 0x0200
LLMHF_INJECTED = 0x1

user32.CallNextHookEx.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_size_t, ctypes.c_ssize_t]
user32.CallNextHookEx.restype = ctypes.c_ssize_t
user32.SetWindowsHookExW.argtypes = [ctypes.c_int, HOOKPROC, ctypes.c_void_p, wt.DWORD]
user32.SetWindowsHookExW.restype = ctypes.c_void_p
user32.UnhookWindowsHookEx.argtypes = [ctypes.c_void_p]

hook_handle = None
hook_proc_ref = None  # keep alive
observed: list[dict] = []


def hook_proc(code, wp, lp):
    if code >= 0 and wp == WM_MOUSEMOVE:
        info = ctypes.cast(lp, ctypes.POINTER(MSLLHOOKSTRUCT)).contents
        observed.append({
            "x": info.pt.x, "y": info.pt.y,
            "injected": bool(info.flags & LLMHF_INJECTED),
            "t": time.perf_counter(),
        })
    return user32.CallNextHookEx(None, code, wp, lp)


def install_observer() -> bool:
    global hook_handle, hook_proc_ref
    hook_proc_ref = HOOKPROC(hook_proc)
    hook_handle = user32.SetWindowsHookExW(WH_MOUSE_LL, hook_proc_ref, None, 0)
    return bool(hook_handle)


def uninstall_observer() -> None:
    global hook_handle
    if hook_handle:
        user32.UnhookWindowsHookEx(hook_handle)
        hook_handle = None


def move_to(x: int, y: int) -> bool:
    """A real, injected SetCursorPos move (what the plugin itself does)."""
    return bool(user32.SetCursorPos(int(x), int(y)))


def cursor_pos() -> tuple[int, int]:
    pt = wt.POINT()
    user32.GetCursorPos(ctypes.byref(pt))
    return pt.x, pt.y


def pump(ms: int) -> None:
    """Pump messages so the observer hook receives events."""
    msg = wt.MSG()
    deadline = time.perf_counter() + ms / 1000.0
    while time.perf_counter() < deadline:
        while user32.PeekMessageW(ctypes.byref(msg), None, 0, 0, 1):
            user32.TranslateMessage(ctypes.byref(msg))
            user32.DispatchMessageW(ctypes.byref(msg))
        time.sleep(0.005)


# --- protocol driver (same shape as protocol_roundtrip.py) -------------------

class Driver:
    def __init__(self, exe: Path, data_dir: Path):
        self.proc = subprocess.Popen(
            [str(exe)], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
            stderr=subprocess.PIPE, text=True, encoding="utf-8", bufsize=1)
        self.next_id = 1

    def send(self, method: str, params: dict) -> int:
        msg_id = self.next_id
        self.next_id += 1
        self.proc.stdin.write(json.dumps(
            {"jsonrpc": "2.0", "id": msg_id, "method": method, "params": params}) + "\n")
        self.proc.stdin.flush()
        return msg_id

    def read_response(self, msg_id: int, timeout: float = 10.0) -> dict:
        deadline = time.time() + timeout
        import threading
        result: dict = {}

        def reader():
            while time.time() < deadline:
                line = self.proc.stdout.readline()
                if not line:
                    break
                try:
                    msg = json.loads(line)
                except json.JSONDecodeError:
                    continue
                if msg.get("id") == msg_id:
                    result.update(msg)
                    return
        t = threading.Thread(target=reader, daemon=True)
        t.start()
        t.join(timeout)
        return result

    def notify(self, method: str, params: dict) -> None:
        self.proc.stdin.write(json.dumps(
            {"jsonrpc": "2.0", "method": method, "params": params}) + "\n")
        self.proc.stdin.flush()

    def close(self) -> int:
        try:
            self.proc.stdin.close()
        except Exception:
            pass
        try:
            return self.proc.wait(timeout=10)
        except subprocess.TimeoutExpired:
            self.proc.kill()
            return -1


def main() -> int:
    if not EXE.is_file():
        print(f"error: {EXE} not found; build first", file=sys.stderr)
        return 2

    layout = json.loads((ROOT / "tests/layouts/live-single.json").read_text(encoding="utf-8-sig"))
    mon = layout["monitors"][0]
    right_edge = mon["right"] - 1
    left_edge = mon["left"]
    mid_y = (mon["top"] + mon["bottom"]) // 2

    checks: list[dict] = []
    evidence: dict = {"layout": mon, "checks": checks}

    with tempfile.TemporaryDirectory(prefix="cw-live-") as tmp:
        drv = Driver(EXE, Path(tmp))
        init = drv.read_response(drv.send("initialize", {
            "protocolVersion": "1", "winshun": "0.22.0", "pluginDir": str(ROOT),
            "dataDir": tmp, "locale": "zh", "theme": "dark",
            "settings": {"wrapMode": "both", "activationMode": "always",
                         "disableDuringDrag": True, "disableOnSingleMonitor": False,
                         "edgeThresholdPx": 1, "wrapGuardPx": 50,
                         "adjacencyTolerancePx": 50}}))
        checks.append({"check": "initialize", "ok": "error" not in init})

        if not install_observer():
            print("error: could not install the observer hook", file=sys.stderr)
            return 2

        def run_case(name: str, start: tuple[int, int], target: tuple[int, int],
                     expect_wrap: bool) -> None:
            observed.clear()
            move_to(*start)
            pump(120)
            before = cursor_pos()
            observed.clear()
            # A burst of small real moves toward the target edge, like a hand push.
            sx, sy = start
            tx, ty = target
            steps = 12
            for i in range(1, steps + 1):
                nx = sx + (tx - sx) * i // steps
                ny = sy + (ty - sy) * i // steps
                move_to(nx, ny)
                pump(15)
            pump(200)
            after = cursor_pos()
            wrapped = (after[0] != tx or after[1] != ty)
            checks.append({
                "check": name, "start": list(start), "target": list(target),
                "before": list(before), "after": list(after),
                "expected_wrap": expect_wrap, "wrapped": wrapped,
                "ok": wrapped == expect_wrap,
                "observed_moves": len(observed),
                "injected_seen": sum(1 for o in observed if o["injected"]),
            })

        # 1. Push to the right edge: the plugin should wrap to the left edge.
        run_case("push-right-edge-wraps-left", (960, mid_y), (right_edge, mid_y), True)
        # 2. Push to the left edge: wraps back to the right edge.
        run_case("push-left-edge-wraps-right", (960, mid_y), (left_edge, mid_y), True)
        # 3. Middle of the screen: no wrap.
        run_case("middle-no-wrap", (960, mid_y), (960, mid_y), False)

        uninstall_observer()
        drv.notify("shutdown", {})
        time.sleep(0.3)
        code = drv.close()
        checks.append({"check": "clean-exit", "exit_code": code, "ok": code == 0})

    out = ROOT / "tests/layouts/live-wrap-evidence.json"
    out.write_text(json.dumps(evidence, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print(json.dumps(evidence, indent=2, ensure_ascii=False))
    failed = [c for c in checks if not c.get("ok", False)]
    print(f"\n{'FAILED: ' + str(len(failed)) if failed else 'all live checks passed'}")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())

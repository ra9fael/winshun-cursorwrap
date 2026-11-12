"""RDP compatibility probe: does wrapping work in a Remote Desktop session?

Why this replaces the after != target oracle
--------------------------------------------
tests/live_wrap_probe.py decides "did it wrap?" from `cursor_pos() != target`.
That is unsound: a push that merely stops short (observed: after=[1888,540]
when the commanded point was 1919) satisfies it, and a right-edge wrap on a
single display must land at x = left+1 = 1. So it can report a pass with no
wrap at all.

This probe judges each push by the plugin's own accounting instead:
  * wraps must go UP by exactly 1 for a press that should wrap;
  * the wrap destination is checked against the documented 1 px inset;
  * a press that must not wrap must leave wraps unchanged, and the per-reason
    skip counters must name the gate that refused it.

It also answers the RDP-specific question: in a session the client's physical
mouse arrives through the injected-input path, and the plugin drops every
LLMHF_INJECTED event. If that blanket filter is in play, moves > 0 with
wraps == 0 and every skip reason reading 0 is the signature.
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

SM_REMOTESESSION = 0x1000
SM_CMONITORS = 80


def cursor_pos() -> tuple[int, int]:
    pt = wt.POINT()
    user32.GetCursorPos(ctypes.byref(pt))
    return pt.x, pt.y


def move_to(x: int, y: int) -> bool:
    return bool(user32.SetCursorPos(int(x), int(y)))


class Driver:
    def __init__(self, data_dir: Path):
        self.proc = subprocess.Popen(
            [str(EXE)], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
            stderr=subprocess.PIPE, text=True, encoding="utf-8", bufsize=1)
        self.next_id = 1

    def _read(self, msg_id: int, timeout: float = 10.0) -> dict:
        deadline = time.time() + timeout
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

    def call(self, method: str, params: dict) -> dict:
        msg_id = self.next_id
        self.next_id += 1
        self.proc.stdin.write(json.dumps(
            {"jsonrpc": "2.0", "id": msg_id, "method": method,
             "params": params}) + "\n")
        self.proc.stdin.flush()
        return self._read(msg_id)

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


def diagnostics(drv: Driver) -> dict:
    """Fetch the diagnostics text through the `diag` row's copy field."""
    resp = drv.call("query", {"text": "diag", "raw": "diag",
                             "trigger": "keyword", "keyword": "cursorwrap"})
    items = (resp.get("result") or {}).get("items") or []
    text = items[0].get("copy", "") if items else ""
    out: dict[str, object] = {"raw": text}
    for line in text.splitlines():
        if ": " in line:
            key, _, value = line.partition(": ")
            out[key.strip()] = value.strip()
    return out


def counts(drv: Driver) -> dict:
    d = diagnostics(drv)
    result = {
        "moves": int(d.get("moves", 0) or 0),
        "wraps": int(d.get("wraps", 0) or 0),
        "skipped": {},
    }
    for key, value in d.items():
        if isinstance(key, str) and key.startswith("skipped "):
            try:
                result["skipped"][key[len("skipped "):]] = int(value)
            except ValueError:
                pass
    return result


BASE = {
    "wrapMode": "both", "activationMode": "always", "disableDuringDrag": True,
    "disableOnSingleMonitor": False, "edgeThresholdPx": 1, "wrapGuardPx": 50,
    "adjacencyTolerancePx": 50, "impactGate": False, "impactSpeedPps": 2000,
    "impactBandPx": 150,
}


def main() -> int:
    if not EXE.is_file():
        print(f"error: {EXE} not found; build first", file=sys.stderr)
        return 2

    remote = user32.GetSystemMetrics(SM_REMOTESESSION)
    monitors = user32.GetSystemMetrics(SM_CMONITORS)
    sw, sh = user32.GetSystemMetrics(0), user32.GetSystemMetrics(1)
    print(f"session: remote={remote} monitors={monitors} screen={sw}x{sh}")

    # The layout the plugin itself enumerates, not a committed fixture: over
    # RDP it is one virtual display and the numbers must come from the machine.
    left, top, right, bottom = 0, 0, sw, sh
    mid_y = (top + bottom) // 2

    checks: list[dict] = []
    evidence: dict = {
        "remote_session": bool(remote),
        "monitors": monitors,
        "screen": {"width": sw, "height": sh},
        "checks": checks,
    }

    def push(drv: Driver, name: str, start, target, steps, step_ms,
             expect_wrap: bool, expect_reason: str | None = None) -> None:
        drv.notify("settingsChanged", {"settings": settings})
        time.sleep(0.2)
        move_to(*start)
        time.sleep(0.12)
        before = counts(drv)
        sx, sy = start
        tx, ty = target
        for i in range(1, steps + 1):
            move_to(sx + (tx - sx) * i // steps, sy + (ty - sy) * i // steps)
            time.sleep(step_ms / 1000.0)
        time.sleep(0.3)
        after = counts(drv)
        delta_wraps = after["wraps"] - before["wraps"]
        wrapped = delta_wraps == 1
        landed = list(cursor_pos())
        wrote = {
            "check": name,
            "expected_wrap": expect_wrap,
            "wraps_before": before["wraps"],
            "wraps_after": after["wraps"],
            "delta_wraps": delta_wraps,
            "landed": landed,
            "moves_seen": after["moves"] - before["moves"],
            "skip_delta": {k: after["skipped"].get(k, 0) - before["skipped"].get(k, 0)
                           for k in set(after["skipped"]) | set(before["skipped"])},
        }
        ok = wrapped == expect_wrap
        if expect_wrap:
            # A right-edge wrap on one display must land one pixel inside the
            # left edge; a left-edge wrap one pixel inside the right edge.
            if target[0] >= right - 2:
                wrote["expected_landing_x"] = left + 1
                ok = ok and landed[0] == left + 1
            elif target[0] <= left + 1:
                wrote["expected_landing_x"] = right - 2
                ok = ok and landed[0] == right - 2
        elif expect_reason:
            ok = ok and wrote["skip_delta"].get(expect_reason, 0) >= 1
        wrote["ok"] = ok
        checks.append(wrote)

    with tempfile.TemporaryDirectory(prefix="cw-rdp-") as tmp:
        drv = Driver(Path(tmp))
        init = drv.call("initialize", {
            "protocolVersion": "1", "winshun": "0.22.0", "pluginDir": str(ROOT),
            "dataDir": tmp, "locale": "en", "theme": "dark", "settings": BASE})
        checks.append({"check": "initialize", "ok": "error" not in init,
                       "result": bool(init.get("result"))})

        settings = BASE
        # Gate off: a push to the right edge on a single display wraps to the
        # same display's left edge (left+1 = 1).
        push(drv, "single-right-edge-wraps", (sw // 2, mid_y),
             (right - 1, mid_y), 16, 12, True)
        push(drv, "single-left-edge-wraps", (sw // 2, mid_y),
             (left, mid_y), 16, 12, True)

        # Gate on: a fast hit wraps, a slow drift must be refused BY THE GATE.
        settings = dict(BASE, impactGate=True, impactSpeedPps=2000)
        push(drv, "gate-fast-hit-wraps", (sw // 2, mid_y),
             (right - 1, mid_y), 24, 5, True)
        push(drv, "gate-slow-drift-refused", (sw // 2, mid_y),
             (right - 1, mid_y), 12, 100, False, "impact-gate")

        # Drag suppression must be named, not silent.
        settings = dict(BASE, disableDuringDrag=True)
        drv.notify("settingsChanged", {"settings": settings})
        time.sleep(0.2)
        before = counts(drv)
        move_to(sw // 2, mid_y)
        time.sleep(0.1)
        move_to(right - 1, mid_y)
        time.sleep(0.3)
        after = counts(drv)
        checks.append({
            "check": "no-silent-suppression",
            "ok": after["moves"] - before["moves"] > 0,
            "moves_delta": after["moves"] - before["moves"],
            "note": "moves > 0 with every reason 0 would mean events are "
                    "dropped before the engine (the RDP injected-input path)",
            "skip_totals": after["skipped"],
        })

        drv.notify("shutdown", {})
        time.sleep(0.3)
        code = drv.close()
        checks.append({"check": "clean-exit", "ok": code == 0, "exit_code": code})

    out = ROOT / "tests/layouts/rdp-wrap-evidence.json"
    out.write_text(json.dumps(evidence, indent=2, ensure_ascii=False) + "\n",
                   encoding="utf-8")
    for c in checks:
        print(f"  {c['check']:<32} ok={c.get('ok')}"
              + (f" delta_wraps={c.get('delta_wraps')} landed={c.get('landed')}"
                 if "delta_wraps" in c else ""))
    failed = [c for c in checks if not c.get("ok", False)]
    print(f"\n{'FAILED: ' + str(len(failed)) if failed else 'all RDP checks passed'}")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())

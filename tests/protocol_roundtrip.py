#!/usr/bin/env python3
"""Drive cursorwrap.exe over the resident JSON-RPC protocol (L3 round trip).

Speaks the exact protocol Win顺 uses on stdin/stdout: one UTF-8 JSON object
per line. Verifies initialize / query / action / settingsChanged / shutdown,
the status row, the three actions, cancel tolerance and a clean exit.

Usage: python tests/protocol_roundtrip.py [--exe path] [--json]
Standard library only.
"""
from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DEFAULT_EXE = ROOT / "cursorwrap.exe"

failures: list[str] = []
checks = 0


def check(cond: bool, label: str, extra: str = "") -> None:
    global checks
    checks += 1
    if cond:
        print(f"  ok   {label}")
    else:
        failures.append(label)
        print(f"  FAIL {label}{(' :: ' + extra) if extra else ''}")


class Driver:
    def __init__(self, exe: Path, data_dir: Path):
        self.proc = subprocess.Popen(
            [str(exe)],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            encoding="utf-8",
            bufsize=1,
        )
        self.next_id = 1
        self.data_dir = data_dir
        self.pending: list[str] = []

    def send(self, method: str, params: dict) -> int:
        msg_id = self.next_id
        self.next_id += 1
        line = json.dumps({"jsonrpc": "2.0", "id": msg_id, "method": method, "params": params})
        self.proc.stdin.write(line + "\n")
        self.proc.stdin.flush()
        return msg_id

    def notify(self, method: str, params: dict) -> None:
        line = json.dumps({"jsonrpc": "2.0", "method": method, "params": params})
        self.proc.stdin.write(line + "\n")
        self.proc.stdin.flush()

    def read_until_response(self, msg_id: int, timeout: float = 10.0) -> dict:
        deadline = time.time() + timeout
        while time.time() < deadline:
            line = self.proc.stdout.readline()
            if not line:
                break
            line = line.strip()
            if not line:
                continue
            try:
                msg = json.loads(line)
            except json.JSONDecodeError:
                continue
            if msg.get("id") == msg_id and ("result" in msg or "error" in msg):
                return msg
            self.pending.append(line)
        raise TimeoutError(f"no response for id {msg_id}")

    def initialize(self) -> dict:
        msg_id = self.send("initialize", {
            "protocolVersion": "1",
            "winshun": "0.22.0",
            "pluginDir": str(ROOT),
            "dataDir": str(self.data_dir),
            "locale": "zh",
            "theme": "dark",
            "settings": default_settings(),
        })
        return self.read_until_response(msg_id)

    def query(self, text: str = "") -> dict:
        msg_id = self.send("query", {
            "queryId": self.next_id,
            "command": "cursorwrap",
            "trigger": "keyword",
            "keyword": "cursorwrap",
            "text": text,
            "raw": f"cursorwrap {text}".strip(),
            "limit": 200,
        })
        return self.read_until_response(msg_id)

    def action(self, action: str, item: dict, text: str = "") -> dict:
        msg_id = self.send("action", {
            "command": "cursorwrap",
            "action": action,
            "text": text,
            "item": item,
        })
        return self.read_until_response(msg_id)

    def settings_changed(self, settings: dict) -> None:
        self.notify("settingsChanged", {"settings": settings})

    def cancel(self, query_id: int) -> None:
        self.notify("cancel", {"queryId": query_id})

    def shutdown(self) -> dict:
        msg_id = self.send("shutdown", {})
        return self.read_until_response(msg_id)

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

    def stderr_text(self) -> str:
        try:
            return self.proc.stderr.read()
        except Exception:
            return ""


def default_settings(**overrides) -> dict:
    s = {
        "wrapMode": "both",
        "activationMode": "always",
        "disableDuringDrag": True,
        "disableOnSingleMonitor": False,
        "edgeThresholdPx": 1,
        "wrapGuardPx": 50,
        "adjacencyTolerancePx": 50,
    }
    s.update(overrides)
    return s


def items_of(response: dict) -> list[dict]:
    result = response.get("result") or {}
    return result.get("items") or []


def run(exe: Path) -> int:
    if not exe.is_file():
        print(f"error: {exe} not found; build first", file=sys.stderr)
        return 2
    with tempfile.TemporaryDirectory(prefix="cw-l3-") as tmp:
        data_dir = Path(tmp) / "cursorwrap.data"
        data_dir.mkdir()
        drv = Driver(exe, data_dir)

        print("initialize")
        init = drv.initialize()
        check("error" not in init, "initialize has no error", json.dumps(init)[:200])
        check(init.get("result", {}).get("protocolVersion") == "1",
              "initialize reports protocolVersion 1", json.dumps(init.get("result"))[:200])

        print("query (empty)")
        q = drv.query("")
        items = items_of(q)
        check(len(items) == 1, f"status query returns exactly one row (got {len(items)})")
        if items:
            row = items[0]
            check(row.get("id") == "cursorwrap.status",
                  "row id is cursorwrap.status", str(row.get("id")))
            check(bool(row.get("title")), "row has a title", str(row.get("title")))
            check(bool(row.get("subtitle")), "row has a subtitle", str(row.get("subtitle")))
            actions = row.get("actions") or []
            check([a.get("id") for a in actions]
                  == ["toggleEnabled", "cycleMode", "copyDiagnostics"],
                  "three actions in Enter/Ctrl+Enter/Shift+Enter order",
                  str([a.get("id") for a in actions]))
            check("monitor" in json.dumps(row, ensure_ascii=False).lower()
                  or "屏" in json.dumps(row, ensure_ascii=False),
                  "status mentions the display count", json.dumps(row, ensure_ascii=False)[:200])

        print("query with text is ignored (no side effects)")
        q2 = drv.query("anything")
        check(len(items_of(q2)) == 1, "query with text still returns one row")

        print("action: cycleMode x3 returns to both")
        row = items[0] if items else {"id": "cursorwrap.status", "title": "x"}
        modes = []
        for _ in range(3):
            resp = drv.action("cycleMode", row)
            effects = (resp.get("result") or {}).get("effects") or []
            types = [e.get("type") for e in effects]
            check("replace" in types, "cycleMode replaces the row", str(types))
            check("notify" in types, "cycleMode notifies the new mode", str(types))
            notifies = [e.get("text", "") for e in effects if e.get("type") == "notify"]
            modes.append(notifies[0] if notifies else "")
        check(len(set(modes)) == 3, "three cycles report three distinct modes", str(modes))

        print("action: toggleEnabled")
        resp = drv.action("toggleEnabled", row)
        effects = (resp.get("result") or {}).get("effects") or []
        types = [e.get("type") for e in effects]
        check("replace" in types and "notify" in types,
              "toggleEnabled replaces the row and notifies", str(types))
        off_items = [e for e in effects if e.get("type") == "replace"]
        if off_items:
            off_row = off_items[0]["items"][0]
            check("off" in off_row.get("title", "").lower()
                  or "关" in off_row.get("title", ""),
                  "the replaced row says the plugin is off", str(off_row.get("title")))
        # Turn it back on so later checks exercise the enabled path.
        drv.action("toggleEnabled", row)

        print("action: copyDiagnostics")
        resp = drv.action("copyDiagnostics", row)
        effects = (resp.get("result") or {}).get("effects") or []
        copies = [e.get("text", "") for e in effects if e.get("type") == "copy"]
        check(len(copies) == 1, "copyDiagnostics emits one copy effect", str(effects)[:200])
        if copies:
            diag = copies[0]
            for needle in ("wrap mode", "activation", "monitors", "outer edges", "wraps"):
                check(needle in diag, f"diagnostics contains '{needle}'")
            check("ra9fael.cursorwrap diagnostics" in diag, "diagnostics header present")

        print("action: unknown id")
        resp = drv.action("nope", row)
        effects = (resp.get("result") or {}).get("effects") or []
        replaced = [e for e in effects if e.get("type") == "replace"]
        check(bool(replaced) and replaced[0]["items"][0].get("valid") is False,
              "unknown action returns an invalid row, not a fake success",
              str(effects)[:200])

        print("settingsChanged: valid values apply")
        drv.settings_changed(default_settings(wrapMode="vertical", activationMode="ctrl"))
        q3 = drv.query("")
        sub = items_of(q3)[0].get("subtitle", "") if items_of(q3) else ""
        check("vertical" in sub or "上下" in sub,
              "wrapMode=vertical shows up in the status row", sub)
        check("ctrl" in sub.lower() or "Ctrl" in sub, "activationMode=ctrl shows up", sub)

        print("settingsChanged: out-of-range values fall back to defaults")
        drv.settings_changed(default_settings(wrapMode="vertical", edgeThresholdPx=999))
        q4 = drv.query("")
        check(len(items_of(q4)) == 1, "status still answers after a bad setting value")

        print("cancel tolerance")
        drv.cancel(12345)  # a queryId that was never issued must be harmless
        q5 = drv.query("")
        check(len(items_of(q5)) == 1, "a stray cancel does not break the next query")

        print("shutdown")
        resp = drv.shutdown()
        check("error" not in resp, "shutdown has no error", json.dumps(resp)[:200])
        code = drv.close()
        check(code == 0, f"process exits with 0 (got {code})", drv.stderr_text()[:400])

    print()
    if failures:
        print(f"FAILED: {len(failures)} of {checks} checks")
        for f in failures:
            print(f"  - {f}")
        return 1
    print(f"all {checks} protocol checks passed")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--exe", default=str(DEFAULT_EXE), help="path to cursorwrap.exe")
    args = ap.parse_args()
    return run(Path(args.exe))


if __name__ == "__main__":
    sys.exit(main())

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
        # The real host persists winshun/settings/update and sends the merged
        # values on the next initialize. Keep that host-side state in the
        # protocol fixture too; otherwise a restart test silently resets the
        # plugin to the manifest defaults and falsely reports a persistence bug.
        self.host_settings = default_settings()

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
            if msg.get("method") == "winshun/settings/update":
                self.host_settings.update(msg.get("params", {}).get("settings", {}))
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
            "settings": self.host_settings.copy(),
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
        self.host_settings.update(settings)
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
        "impactGate": False,
        "impactSpeedPps": 2000,
        "impactFlash": False,
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

        print("query (empty) returns the settings list")
        q = drv.query("")
        items = items_of(q)
        check(len(items) >= 5, f"empty query returns the settings list (got {len(items)})")
        if items:
            row = items[0]
            check(row.get("id") == "cursorwrap.status",
                  "first row is the status row", str(row.get("id")))
            check(bool(row.get("title")), "status row has a title", str(row.get("title")))
            check(bool(row.get("subtitle")), "status row has a subtitle", str(row.get("subtitle")))
            actions = row.get("actions") or []
            check([a.get("id") for a in actions] == ["toggleEnabled"],
                  "status row has only toggleEnabled (Enter)",
                  str([a.get("id") for a in actions]))
            ids = [r.get("id") for r in items]
            for needle in ("cursorwrap.row.impact", "cursorwrap.row.speed",
                           "cursorwrap.row.band", "cursorwrap.row.mode",
                           "cursorwrap.row.activation", "cursorwrap.row.guard",
                           "cursorwrap.row.edge", "cursorwrap.row.tol",
                           "cursorwrap.row.drag", "cursorwrap.row.single",
                           "cursorwrap.row.tuner", "cursorwrap.row.data",
                           "cursorwrap.diag", "cursorwrap.row.reset"):
                check(needle in ids, f"settings list contains {needle}", str(ids))
            # Drill-down rows carry setQuery so Enter fills the search box.
            for rid in ("cursorwrap.row.speed", "cursorwrap.row.band",
                        "cursorwrap.row.activation", "cursorwrap.row.guard",
                        "cursorwrap.row.edge", "cursorwrap.row.tol",
                        "cursorwrap.row.drag", "cursorwrap.row.single",
                        "cursorwrap.row.tuner", "cursorwrap.row.data"):
                row = next(r for r in items if r.get("id") == rid)
                check(bool(row.get("setQuery")),
                      f"{rid} has setQuery for drill-down", str(row.get("setQuery")))

        print("query: every manifest setting is reachable from the search bar")
        # The manifest declares ten settings; each must have a row (directly
        # or through a submenu) so the two surfaces cannot drift apart.
        manifest = json.loads((ROOT / "plugin.json").read_text(encoding="utf-8"))
        setting_keys = [s.get("key") for s in manifest.get("settings", [])]
        check(len(setting_keys) == 14, f"manifest declares fourteen settings (got {len(setting_keys)})")
        covered = {
            "wrapMode": "cursorwrap.row.mode",
            "activationMode": "cursorwrap.row.activation",
            "disableDuringDrag": "cursorwrap.row.drag",
            "disableOnSingleMonitor": "cursorwrap.row.single",
            "edgeThresholdPx": "cursorwrap.row.edge",
            "wrapGuardPx": "cursorwrap.row.guard",
            "adjacencyTolerancePx": "cursorwrap.row.tol",
            "impactGate": "cursorwrap.row.impact",
            "impactSpeedPps": "cursorwrap.row.speed",
            "impactBandPx": "cursorwrap.row.band",
            "impactAccelPps2": "cursorwrap.row.accel",
            "tuningOverlay": "cursorwrap.row.tuner",
            "impactFlash": "cursorwrap.row.flash",
            "dataWindow": "cursorwrap.row.data",
        }
        list_ids = [r.get("id") for r in items_of(drv.query(""))]
        for key in setting_keys:
            rid = covered.get(key)
            check(rid is not None and rid in list_ids,
                  f"setting {key} has a row in the list", f"{key} -> {rid}")

        print("query with text is ignored (no side effects)")
        q2 = drv.query("anything")
        check(len(items_of(q2)) == 1, "query with text still returns one row")

        print("query: search-bar commands change settings")
        # `impact` alone opens the submenu (it does not toggle any more).
        q3 = drv.query("impact")
        items3 = items_of(q3)
        check(items3 and items3[0].get("id") == "cursorwrap.back",
              "impact query opens the impact submenu (first row is back)",
              str([r.get("id") for r in items3]))
        impact_row = next((r for r in items3 if r.get("id") == "cursorwrap.impact.state"), None)
        check(impact_row is not None, "impact submenu has the toggle row")
        # `impact on` / `impact off` set the state directly.
        drv.query("impact on")
        q4 = drv.query("impact")
        impact_on = next((r for r in items_of(q4) if r.get("id") == "cursorwrap.impact.state"), None)
        check(impact_on is not None and ("开" in impact_on["title"]),
              "impact on turns the gate on", impact_on["title"] if impact_on else "")
        drv.query("impact off")
        q4b = drv.query("impact")
        impact_off = next((r for r in items_of(q4b) if r.get("id") == "cursorwrap.impact.state"), None)
        check(impact_off is not None and ("关" in impact_off["title"]),
              "impact off turns the gate off", impact_off["title"] if impact_off else "")
        # A speed command turns the gate on with the given value and shows
        # the speed submenu.
        q5 = drv.query("speed 3500")
        items5 = items_of(q5)
        check(items5 and items5[0].get("id") == "cursorwrap.back",
              "speed 3500 opens the speed submenu", str([r.get("id") for r in items5]))
        # The status row (back in the list) reports the new speed.
        q5b = drv.query("")
        status5 = items_of(q5b)[0]
        check("3500" in status5.get("subtitle", ""),
              "speed 3500 shows in the status row", status5.get("subtitle", ""))
        # An unknown word yields an invalid row, not a fake success.
        q6 = drv.query("hello world")
        row6 = items_of(q6)[0]
        check(row6.get("id") == "cursorwrap.unknown" and row6.get("valid") is False,
              "unknown words give an invalid hint row", str(row6)[:200])
        # reset clears the overrides (the status row drops the source note).
        q7 = drv.query("reset")
        items7 = items_of(q7)
        status7 = items7[0] if items7 else {}
        check("搜索栏覆盖" not in status7.get("subtitle", "")
              and "overrides active" not in status7.get("subtitle", ""),
              "reset clears the override note", status7.get("subtitle", ""))

        print("query: drag / single / tune submenus reach their settings")
        qd = drv.query("drag")
        itemsd = items_of(qd)
        check(itemsd and itemsd[0].get("id") == "cursorwrap.back",
              "drag query opens the drag submenu", str([r.get("id") for r in itemsd]))
        drv.query("drag on")
        qd2 = drv.query("")
        drag_row = next(r for r in items_of(qd2) if r.get("id") == "cursorwrap.row.drag")
        check("开" in drag_row["title"] or "on" in drag_row["title"],
              "drag on shows as on", drag_row["title"])
        drv.query("drag off")
        qd3 = drv.query("")
        drag_row2 = next(r for r in items_of(qd3) if r.get("id") == "cursorwrap.row.drag")
        check("关" in drag_row2["title"] or "off" in drag_row2["title"],
              "drag off shows as off", drag_row2["title"])

        qs = drv.query("single")
        itemss = items_of(qs)
        check(itemss and itemss[0].get("id") == "cursorwrap.back",
              "single query opens the single-monitor submenu", str([r.get("id") for r in itemss]))
        drv.query("single on")
        qs2 = drv.query("")
        single_row = next(r for r in items_of(qs2) if r.get("id") == "cursorwrap.row.single")
        check("开" in single_row["title"] or "on" in single_row["title"],
              "single on shows as on", single_row["title"])
        drv.query("single off")

        print("query: impact flash setting is shared by the card and search bar")
        drv.query("flash on")
        flash_row = next(r for r in items_of(drv.query(""))
                         if r.get("id") == "cursorwrap.row.flash")
        check("开" in flash_row["title"] or "on" in flash_row["title"],
              "flash on is reflected in the settings list", flash_row["title"])
        # Clear the search-bar override before testing a card change: the plugin
        # intentionally keeps search-bar overrides above unrelated card updates,
        # because WinShun has no plugin->host settings-write channel.
        drv.query("reset")
        drv.settings_changed(default_settings(impactFlash=True))
        time.sleep(0.2)
        flash_row = next(r for r in items_of(drv.query(""))
                         if r.get("id") == "cursorwrap.row.flash")
        check("开" in flash_row["title"] or "on" in flash_row["title"],
              "the settings card can turn the impact flash on", flash_row["title"])
        drv.settings_changed(default_settings(impactFlash=False))
        time.sleep(0.2)
        flash_row = next(r for r in items_of(drv.query(""))
                         if r.get("id") == "cursorwrap.row.flash")
        check("关" in flash_row["title"] or "off" in flash_row["title"],
              "the settings card can turn the impact flash off", flash_row["title"])
        drv.query("光效")
        check(any(i.get("id") == "cursorwrap.row.flash" for i in items_of(drv.query(""))),
              "the localized flash command is accepted")
        drv.query("flash off")

        print("query: the tuning overlay has its own submenu and parameters")
        qt = drv.query("tune")
        itemst = items_of(qt)
        check(itemst and itemst[0].get("id") == "cursorwrap.back",
              "tune opens the overlay submenu (first row is back)",
              str([r.get("id") for r in itemst]))
        tuner_ids = [r.get("id") for r in itemst]
        for needle in ("cursorwrap.tuner.state", "cursorwrap.tuner.band",
                       "cursorwrap.tuner.speed", "cursorwrap.tuner.guard",
                       "cursorwrap.tuner.edge"):
            check(needle in tuner_ids, f"overlay submenu contains {needle}", str(tuner_ids))
        state_row = next(r for r in itemst if r.get("id") == "cursorwrap.tuner.state")
        check("关" in state_row["title"] or "off" in state_row["title"],
              "the overlay starts off", state_row["title"])
        # `tune on` turns it on without the wildcard toggle of a bare `tune`.
        drv.query("tune on")
        qt2 = drv.query("tune")
        state_on = next(r for r in items_of(qt2) if r.get("id") == "cursorwrap.tuner.state")
        check("开" in state_on["title"] or "on" in state_on["title"],
              "tune on turns the overlay on", state_on["title"])
        drv.query("tune off")
        qt3 = drv.query("tune")
        state_off = next(r for r in items_of(qt3) if r.get("id") == "cursorwrap.tuner.state")
        check("关" in state_off["title"] or "off" in state_off["title"],
              "tune off turns the overlay off", state_off["title"])
        # The overlay's parameter rows drill into the same submenus.
        for rid in ("cursorwrap.tuner.band", "cursorwrap.tuner.speed",
                    "cursorwrap.tuner.guard", "cursorwrap.tuner.edge"):
            row = next(r for r in itemst if r.get("id") == rid)
            check(bool(row.get("setQuery")),
                  f"{rid} has setQuery for drill-down", str(row.get("setQuery")))

        print("query: the data window has its own submenu")
        qw = drv.query("data")
        itemsw = items_of(qw)
        check(itemsw and itemsw[0].get("id") == "cursorwrap.back",
              "data opens the data-window submenu", str([r.get("id") for r in itemsw]))
        window_ids = [r.get("id") for r in itemsw]
        for needle in ("cursorwrap.data.state", "cursorwrap.data.reasons",
                       "cursorwrap.data.health"):
            check(needle in window_ids, f"data submenu contains {needle}", str(window_ids))
        data_state = next(r for r in itemsw if r.get("id") == "cursorwrap.data.state")
        check("关" in data_state["title"] or "off" in data_state["title"],
              "the data window starts off", data_state["title"])
        drv.query("data on")
        qw2 = drv.query("data")
        window_on = next(r for r in items_of(qw2) if r.get("id") == "cursorwrap.data.state")
        check("开" in window_on["title"] or "on" in window_on["title"],
              "data on turns the window on", window_on["title"])
        # The settings page and the search bar share the switch: the list row
        # must agree with the submenu without another command.
        list_after = items_of(drv.query(""))
        data_row = next(r for r in list_after if r.get("id") == "cursorwrap.row.data")
        check("开" in data_row["title"] or "on" in data_row["title"],
              "the settings list agrees the data window is on", data_row["title"])
        drv.query("data off")
        qw3 = drv.query("data")
        window_off = next(r for r in items_of(qw3) if r.get("id") == "cursorwrap.data.state")
        check("关" in window_off["title"] or "off" in window_off["title"],
              "data off turns the window off", window_off["title"])
        # `window` is an alias (the row title's own word).
        check(items_of(drv.query("window"))[0].get("id") == "cursorwrap.back",
              "window is an alias for data")

        print("query: trace and best")
        qtr = drv.query("trace")
        tr_items = items_of(qtr)
        check(tr_items and tr_items[0].get("id") == "cursorwrap.trace",
              "trace returns the trace row", str([r.get("id") for r in tr_items]))
        csv_text = tr_items[0].get("copy", "") if tr_items else ""
        check("measured_pps" in csv_text, "the trace CSV has a header",
              csv_text[:80])
        qb = drv.query("best")
        b_items = items_of(qb)
        check(b_items and b_items[0].get("id") == "cursorwrap.back",
              "best opens the suggestion submenu", str([r.get("id") for r in b_items]))
        # No arrivals were recorded in a protocol session: the plugin must say
        # so rather than fabricate a gate.
        check(any(r.get("id") == "cursorwrap.best.none" for r in b_items),
              "best says 'not enough data' without a trace",
              str([r.get("id") for r in b_items]))
        check(drv.query("clear") and items_of(drv.query(""))[0].get("id") == "cursorwrap.status",
              "clear is accepted and the list still answers")

        print("action: cycleMode x3 returns to both")
        # The cycleMode action runs on the mode row, not the status row.
        list_items = items_of(drv.query(""))
        mode_row = next(r for r in list_items if r.get("id") == "cursorwrap.row.mode")
        modes = []
        for _ in range(3):
            resp = drv.action("cycleMode", mode_row)
            effects = (resp.get("result") or {}).get("effects") or []
            types = [e.get("type") for e in effects]
            check("replace" in types, "cycleMode replaces the list", str(types))
            check("notify" in types, "cycleMode notifies the new mode", str(types))
            notifies = [e.get("text", "") for e in effects if e.get("type") == "notify"]
            modes.append(notifies[0] if notifies else "")
        check(len(set(modes)) == 3, "three cycles report three distinct modes", str(modes))

        print("action: toggleEnabled")
        list_items = items_of(drv.query(""))
        status_row = list_items[0]
        resp = drv.action("toggleEnabled", status_row)
        effects = (resp.get("result") or {}).get("effects") or []
        types = [e.get("type") for e in effects]
        check("replace" in types and "notify" in types,
              "toggleEnabled replaces the list and notifies", str(types))
        off_items = [e for e in effects if e.get("type") == "replace"]
        if off_items:
            off_status = off_items[0]["items"][0]
            check("off" in off_status.get("title", "").lower()
                  or "关" in off_status.get("title", ""),
                  "the replaced status row says the plugin is off", str(off_status.get("title")))
        # Turn it back on so later checks exercise the enabled path.
        drv.action("toggleEnabled", off_items[0]["items"][0] if off_items else status_row)

        print("action: copyDiagnostics")
        list_items = items_of(drv.query(""))
        status_row = list_items[0]
        resp = drv.action("copyDiagnostics", status_row)
        effects = (resp.get("result") or {}).get("effects") or []
        copies = [e.get("text", "") for e in effects if e.get("type") == "copy"]
        check(len(copies) == 1, "copyDiagnostics emits one copy effect", str(effects)[:200])
        if copies:
            diag = copies[0]
            for needle in ("wrap mode", "activation", "monitors", "outer edges", "wraps"):
                check(needle in diag, f"diagnostics contains '{needle}'")
            # The per-reason split is what makes "it never wraps" diagnosable.
            check("skipped " in diag or "suppressed" in diag,
                  "diagnostics carries the skip counters", diag[:400])
            check("hook responsive" in diag,
                  "diagnostics carries the hook health", diag[:400])
            check("ra9fael.cursorwrap diagnostics" in diag, "diagnostics header present")

        print("query: diag returns a copyable diagnostics row")
        qd = drv.query("diag")
        drow = items_of(qd)[0]
        check(drow.get("id") == "cursorwrap.diag",
              "diag returns the diagnostics row", str(drow.get("id")))
        copy_text = drow.get("copy", "")
        check(copy_text.startswith("ra9fael.cursorwrap diagnostics"),
              "the row's copy field carries the diagnostics", copy_text[:120])
        dactions = drow.get("actions") or []
        check([a.get("id") for a in dactions] == ["copyDiagRow"],
              "the diagnostics row's Enter runs copyDiagRow", str(dactions))
        dresp = drv.action("copyDiagRow", drow)
        deffects = (dresp.get("result") or {}).get("effects") or []
        dcopies = [e.get("text", "") for e in deffects if e.get("type") == "copy"]
        check(len(dcopies) == 1 and "wrap mode" in dcopies[0],
              "copyDiagRow copies the diagnostics", str(deffects)[:200])
        # 诊断 works the same (zh alias).
        qd2 = drv.query("诊断")
        check(items_of(qd2)[0].get("id") == "cursorwrap.diag",
              "诊断 returns the diagnostics row too", str(items_of(qd2)[0].get("id")))

        print("action: unknown id")
        resp = drv.action("nope", status_row)
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
        check(len(items_of(q4)) >= 5, "settings list still answers after a bad setting value")

        print("settingsChanged: impact gate on shows in diagnostics")
        drv.settings_changed(default_settings(impactGate=True, impactSpeedPps=3000))
        list_items = items_of(drv.query(""))
        status_row = list_items[0]
        resp = drv.action("copyDiagnostics", status_row)
        effects = (resp.get("result") or {}).get("effects") or []
        copies = [e.get("text", "") for e in effects if e.get("type") == "copy"]
        check(bool(copies) and "impact gate: on" in copies[0],
              "diagnostics reports the impact gate state", str(copies)[:200])
        check(bool(copies) and "impact speed: 3000 px/s" in copies[0],
              "diagnostics reports the impact speed", str(copies)[:200])
        drv.settings_changed(default_settings(impactGate=False, impactSpeedPps=99999))
        list_items = items_of(drv.query(""))
        status_row = list_items[0]
        resp = drv.action("copyDiagnostics", status_row)
        effects = (resp.get("result") or {}).get("effects") or []
        copies = [e.get("text", "") for e in effects if e.get("type") == "copy"]
        check(bool(copies) and "impact gate: off" in copies[0],
              "the gate can be turned off again", str(copies)[:200])

        print("search-bar settings survive a restart (the user's report)")
        # The reader used to skip impactBandPx and impactAccelPps2, so
        # "accel 2000" survived in settings.json and the menu showed
        # "off (speed only)" after the next start.
        drv.query("accel 2000")
        drv.query("band 250")
        saved = (data_dir / "settings.json").read_text(encoding="utf-8")
        check("impactAccelPps2" in saved, "the accel setting reaches settings.json", saved[:200])
        check("impactBandPx" in saved, "the band setting reaches settings.json", saved[:200])
        drv.close()

        def current_accel(driver) -> list[str]:
            # Only the current row carries a subtitle, and it is localised
            # ("current" in English, "\u5f53\u524d" in Chinese), so presence is the
            # language-neutral signal.
            return [i.get("title") for i in items_of(driver.query("accel"))
                    if i.get("id") == "cursorwrap.accel" and i.get("subtitle")]

        restarted = Driver(exe, data_dir)
        restarted.host_settings = drv.host_settings.copy()
        restarted.initialize()
        check(current_accel(restarted) == ["2000 px/s\u00b2"],
              "the accel setting is still 2000 after a restart",
              str(current_accel(restarted)))

        # Explicitly disabling acceleration in the card must beat the old
        # search value, including when zero is the manifest default.
        restarted.settings_changed(default_settings(wrapMode="vertical", impactAccelPps2=0))
        time.sleep(0.4)
        check(current_accel(restarted) != ["2000 px/s\u00b2"],
              "the card can disable previously pinned acceleration",
              str(current_accel(restarted)))
        restarted.close()

        # The remaining checks assume defaults, so start from a clean data dir.
        data_dir2 = Path(tmp) / "cursorwrap2.data"
        data_dir2.mkdir()
        drv = Driver(exe, data_dir2)
        drv.initialize()

        print("every numeric submenu shows the value in effect (the user's report)")
        # The card in WinShun's settings page accepts any number; the numeric
        # submenus list a fixed set. A card-only value used to produce a menu with
        # NO row highlighted and no way to pick that value again. Worse, 0.6.0
        # changed impactBandPx's default to 400 while the presets were
        # {50,80,120,150,200,300,500}, so the DEFAULT state highlighted nothing.
        menu_rows = {
            "speed": ("cursorwrap.speed", "impactSpeedPps"),
            "band": ("cursorwrap.band", "impactBandPx"),
            "accel": ("cursorwrap.accel", "impactAccelPps2"),
            "guard": ("cursorwrap.guard", "wrapGuardPx"),
            "edge": ("cursorwrap.edge", "edgeThresholdPx"),
            "tol": ("cursorwrap.tol", "adjacencyTolerancePx"),
        }

        def highlighted(driver, menu, item_id) -> list[str]:
            return [i.get("title") for i in items_of(driver.query(menu))
                    if i.get("id") == item_id and i.get("subtitle")]

        # (a) the shipped defaults must each be visible in their own menu
        for menu, (item_id, key) in menu_rows.items():
            got = highlighted(drv, menu, item_id)
            check(len(got) == 1,
                  f"the default {key} highlights exactly one row in '{menu}'", str(got))

        # (b) a value the card can set but the presets never offer
        drv.settings_changed(default_settings(
            wrapMode="both", impactGate=True, impactSpeedPps=4321,
            impactBandPx=222, impactAccelPps2=1234,
            edgeThresholdPx=7, wrapGuardPx=77, adjacencyTolerancePx=33))
        time.sleep(0.5)
        for menu, (item_id, key) in menu_rows.items():
            got = highlighted(drv, menu, item_id)
            check(len(got) == 1,
                  f"a card-only {key} highlights exactly one row in '{menu}'", str(got))
        expect = {
            "speed": ["4321 px/s"],
            "band": ["222 px"],
            "accel": ["1234 px/s\u00b2"],
            "guard": ["77 px"],
            "edge": ["7 px"],
            "tol": ["33 px"],
        }
        for menu, want in expect.items():
            got = highlighted(drv, menu, menu_rows[menu][0])
            check(got == want, f"'{menu}' names the card-only value", f"{got} != {want}")

        # (c) and the row is actionable: pressing it re-applies the same value
        q = drv.query("band 222")
        check(any(i.get("id") == "cursorwrap.band" for i in items_of(q)),
              "the card-only band value is still selectable",
              json.dumps(q)[:200])

        # Restore the defaults for the checks that follow.
        drv.settings_changed(default_settings())
        time.sleep(0.4)

        print("cancel tolerance")
        drv.cancel(12345)  # a queryId that was never issued must be harmless
        q5 = drv.query("")
        check(len(items_of(q5)) >= 5, "a stray cancel does not break the next query")

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

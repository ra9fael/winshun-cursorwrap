#!/usr/bin/env python3
"""Drive cursorwrap.exe over the resident JSON-RPC protocol (L3 round trip).

Speaks the exact protocol Win顺 uses on stdin/stdout: one UTF-8 JSON object
per line. Verifies initialize / query / action / settingsChanged / shutdown,
the preview-then-apply semantics of the value commands, the settings
ownership split (host card vs the plugin's tuning file), and a clean exit.

Usage: python tests/protocol_roundtrip.py [--exe path] [--json]
Standard library only.
"""
from __future__ import annotations

import argparse
import json
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
        # values back on the next initialize. Keep that host-side state in the
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
    # Exactly what the host card declares (the manifest's settings): the
    # plugin's tuning keys travel in its own file, not in this payload.
    s = {
        "wrapMode": "both",
        "activationMode": "always",
        "disableDuringDrag": True,
        "disableOnSingleMonitor": False,
        "impactGate": False,
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

        print("query (empty) returns the trimmed home list")
        q = drv.query("")
        items = items_of(q)
        check(len(items) >= 5, f"empty query returns the home list (got {len(items)})")
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
            for needle in ("cursorwrap.row.mode", "cursorwrap.row.impact",
                           "cursorwrap.row.window", "cursorwrap.row.data",
                           "cursorwrap.diag"):
                check(needle in ids, f"home list contains {needle}", str(ids))
            # The tuning numbers live in the window; they are NOT home rows.
            for gone in ("cursorwrap.row.speed", "cursorwrap.row.band",
                         "cursorwrap.row.accel", "cursorwrap.row.guard",
                         "cursorwrap.row.edge", "cursorwrap.row.tol"):
                check(gone not in ids, f"{gone} moved to the tuning window", str(ids))
            check(bool(next(r for r in items if r.get("id") == "cursorwrap.row.window")
                       .get("actions")),
                  "the tuning-window row opens via an action", str(ids))

        print("the manifest declares the host card's five settings; the plugin owns the rest")
        manifest = json.loads((ROOT / "plugin.json").read_text(encoding="utf-8"))
        setting_keys = [s.get("key") for s in manifest.get("settings", [])]
        check(setting_keys == ["wrapMode", "activationMode", "disableDuringDrag",
                               "disableOnSingleMonitor", "impactGate"],
              "the manifest declares the five host-card settings", str(setting_keys))
        check(manifest.get("settingsWindow") is True,
              "the manifest declares settingsWindow (the card opens the tuning window)")

        print("query with text is previewed, not applied (no side effects)")
        q2 = drv.query("speed 3500")
        rows = items_of(q2)
        check(len(rows) == 1 and rows[0].get("id") == "cursorwrap.apply",
              "a value command returns one pending row", str(rows)[:200])
        check("3500" in rows[0].get("title", "") and "待应用" in rows[0].get("title", ""),
              "the pending row names the target value", rows[0].get("title", ""))
        acts = rows[0].get("actions") or []
        check(acts and acts[0].get("id") == "set:speed:3500",
              "the pending row carries the apply action", str(acts))
        check(bool(acts) and acts[0].get("key") == "Enter", "apply sits on Enter", str(acts))
        # The value must NOT have moved: the query only previewed.
        status = items_of(drv.query(""))[0]
        check("3500" not in status.get("subtitle", ""),
              "the speed is unchanged until the action runs", status.get("subtitle", ""))

        print("action: applying the pending change")
        resp = drv.action("set:speed:3500", rows[0])
        effects = (resp.get("result") or {}).get("effects") or []
        check(any(e.get("type") == "replace" for e in effects),
              "apply replaces the list", str(effects)[:200])
        status = items_of(drv.query(""))[0]
        check("3500" in status.get("subtitle", ""),
              "the applied speed shows in the status row", status.get("subtitle", ""))
        # The sent update carries only the host card's keys; the speed is the
        # plugin's own and must not appear in winshun/settings/update.
        # (read_until_response folded any such update into host_settings; the
        # plugin's split keeps impactSpeedPps out of it.)
        check("impactSpeedPps" not in drv.host_settings,
              "the plugin's tuning key is not pushed to the host", str(drv.host_settings))

        print("preview semantics: every value command family")
        families = [
            ("impact on", "set:impact:1"),
            ("impact off", "set:impact:0"),
            ("band 300", "set:band:300"),
            ("accel 0", "set:accel:0"),
            ("mode vertical", "set:mode:1"),
            ("ctrl", "setActivationCtrl"),
            ("shift", "setActivationShift"),
            ("always", "setActivationAlways"),
            ("guard 120", "set:guard:120"),
            ("edge 5", "set:edge:5"),
            ("tol 80", "set:tol:80"),
            ("drag on", "set:drag:1"),
            ("single on", "set:single:1"),
            ("tune on", "set:tuner:1"),
            ("data on", "set:data:1"),
            ("flash on", "set:flash:1"),
            ("reset", "resetDefaults"),
            ("clear", "clearTrace"),
        ]
        for text, action in families:
            rows = items_of(drv.query(text))
            ok = (len(rows) == 1 and rows[0].get("id") == "cursorwrap.apply"
                  and rows[0].get("actions")
                  and rows[0]["actions"][0].get("id") == action)
            check(ok, f"'{text}' previews with action {action}",
                  json.dumps(rows, ensure_ascii=False)[:160])
        rows = items_of(drv.query("accel 2000"))
        check(rows and rows[0].get("actions", [{}])[0].get("id") == "set:accel:2000",
              "'accel 2000' previews the acceleration floor", json.dumps(rows)[:160])
        # Bare flash previews the toggle's TARGET (off when on, on when off):
        # apply flash on, then bare flash must preview set:flash:0.
        drv.action("set:flash:1", items_of(drv.query("flash on"))[0])
        rows = items_of(drv.query("flash"))
        check(rows and rows[0].get("actions", [{}])[0].get("id") == "set:flash:0",
              "bare flash previews the toggle's target", json.dumps(rows)[:160])
        drv.action("set:flash:0", rows[0])

        print("tuning values survive a restart through the plugin's own file")
        drv.action("set:accel:2000", items_of(drv.query("accel 2000"))[0])
        drv.action("set:band:250", items_of(drv.query("band 250"))[0])
        saved = (data_dir / "settings.json").read_text(encoding="utf-8")
        check("impactAccelPps2" in saved, "the accel setting reaches settings.json", saved[:200])
        check("impactBandPx" in saved, "the band setting reaches settings.json", saved[:200])
        # The host card's keys must NOT be in the plugin's file: they are the
        # host's to persist.
        check("impactGate" not in saved and "wrapMode" not in saved,
              "host-card keys stay out of the plugin's file", saved[:200])
        drv.close()

        def current_accel(driver) -> list[str]:
            # Only the current row carries a subtitle ("当前" zh / "current" en).
            return [i.get("title") for i in items_of(driver.query("accel"))
                    if i.get("id") == "cursorwrap.accel" and i.get("subtitle")]

        restarted = Driver(exe, data_dir)
        restarted.host_settings = drv.host_settings.copy()
        restarted.initialize()
        check(current_accel(restarted) == ["2000 px/s\u00b2"],
              "the accel setting is still 2000 after a restart",
              str(current_accel(restarted)))

        # Acceleration is the plugin's own now: the card cannot touch it, and
        # turning it off goes through the same preview-then-apply path.
        restarted.action("set:accel:0", items_of(restarted.query("accel 0"))[0])
        time.sleep(0.2)
        check(current_accel(restarted) != ["2000 px/s\u00b2"],
              "accel 0 (applied) turns the floor off", str(current_accel(restarted)))
        restarted.close()

        # The remaining checks assume defaults, so start from a clean data dir.
        data_dir2 = Path(tmp) / "cursorwrap2.data"
        data_dir2.mkdir()
        drv = Driver(exe, data_dir2)
        drv.initialize()

        print("every numeric submenu shows the value in effect")
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

        for menu, (item_id, key) in menu_rows.items():
            got = highlighted(drv, menu, item_id)
            check(len(got) == 1,
                  f"the default {key} highlights exactly one row in '{menu}'", str(got))

        # Apply a set of non-preset values through the pending rows.
        for text, action in [("speed 4321", "set:speed:4321"), ("band 222", "set:band:222"),
                             ("accel 1234", "set:accel:1234"), ("edge 7", "set:edge:7"),
                             ("guard 77", "set:guard:77"), ("tol 33", "set:tol:33")]:
            drv.action(action, items_of(drv.query(text))[0])
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
            check(got == want, f"'{menu}' names the applied value", f"{got} != {want}")

        # Restore the defaults for the checks that follow.
        drv.action("resetDefaults", items_of(drv.query("reset"))[0])
        time.sleep(0.2)

        print("submenus and read-only rows still answer")
        q3 = drv.query("impact")
        items3 = items_of(q3)
        check(items3 and items3[0].get("id") == "cursorwrap.back",
              "impact query opens the impact submenu (first row is back)",
              str([r.get("id") for r in items3]))
        qt = drv.query("tune")
        itemst = items_of(qt)
        tuner_ids = [r.get("id") for r in itemst]
        for needle in ("cursorwrap.tuner.state", "cursorwrap.tuner.band",
                       "cursorwrap.tuner.speed", "cursorwrap.tuner.guard",
                       "cursorwrap.tuner.edge"):
            check(needle in tuner_ids, f"overlay submenu contains {needle}", str(tuner_ids))
        qw = drv.query("data")
        window_ids = [r.get("id") for r in items_of(qw)]
        for needle in ("cursorwrap.data.state", "cursorwrap.data.reasons",
                       "cursorwrap.data.health"):
            check(needle in window_ids, f"data submenu contains {needle}", str(window_ids))
        check(items_of(drv.query("window"))[0].get("id") == "cursorwrap.back",
              "window is an alias for data")
        qtr = drv.query("trace")
        tr_items = items_of(qtr)
        csv_text = tr_items[0].get("copy", "") if tr_items else ""
        check("measured_pps" in csv_text, "the trace CSV has a header", csv_text[:80])
        qb = drv.query("best")
        b_items = items_of(qb)
        # No arrivals were recorded in a protocol session: the plugin must say
        # so rather than fabricate a gate.
        check(any(r.get("id") == "cursorwrap.best.none" for r in b_items),
              "best says 'not enough data' without a trace",
              str([r.get("id") for r in b_items]))
        check(items_of(drv.query("clear"))[0].get("id") == "cursorwrap.apply",
              "clear previews too (its action applies)")

        print("action: cycleMode x3 returns to both")
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

        print("action: openSettings (the tuning-window entry)")
        resp = drv.action("openSettings", next(
            r for r in items_of(drv.query("")) if r.get("id") == "cursorwrap.row.window"))
        effects = (resp.get("result") or {}).get("effects") or []
        check(any(e.get("type") == "notify" for e in effects),
              "openSettings notifies", str(effects)[:200])

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

        print("settingsChanged: the host card's keys apply")
        drv.settings_changed(default_settings(wrapMode="vertical", activationMode="ctrl",
                                              impactGate=True))
        q3 = drv.query("")
        sub = items_of(q3)[0].get("subtitle", "") if items_of(q3) else ""
        check("vertical" in sub or "仅上下" in sub,
              "wrapMode=vertical shows up in the status row", sub)
        check("ctrl" in sub.lower() or "Ctrl" in sub, "activationMode=ctrl shows up", sub)
        check("开" in sub, "impactGate=true shows up", sub)

        print("settingsChanged: a stale tuning key in the payload is ignored")
        # The host keeps stored values it was once sent; after the split they
        # must not restore themselves over the plugin's newer tuning.
        drv.settings_changed(default_settings(wrapMode="vertical", impactSpeedPps=9))
        rows = items_of(drv.query("speed"))
        got = [i.get("title") for i in rows
               if i.get("id") == "cursorwrap.speed" and i.get("subtitle")]
        check(got != ["9 px/s"], "a stale tuning key in settingsChanged is dropped",
              str(got))

        print("settingsChanged: an absurd value does not break the next query")
        drv.settings_changed(default_settings(wrapMode="nonsense"))
        check(len(items_of(drv.query(""))) >= 5,
              "settings list still answers after a bad setting value")

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

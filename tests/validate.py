#!/usr/bin/env python3
"""L0 static validation: manifest, bilingual docs, packaging.

Usage:
  python tests/validate.py                       # source tree only
  python tests/validate.py --package-dir dist/ci  # also check a built package
  python tests/validate.py --out report.json      # write a machine-readable result

Uses jsonschema when installed; otherwise falls back to hand checks (the CI job
installs it). Standard library otherwise.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
problems: list[str] = []
notes: list[str] = []


def fail(msg: str) -> None:
    problems.append(msg)


def check(cond: bool, msg: str) -> None:
    if not cond:
        fail(msg)


def text_of(rel: str) -> str:
    path = ROOT / rel
    if not path.is_file():
        fail(f"missing {rel}")
        return ""
    return path.read_text(encoding="utf-8-sig")


def validate_manifest() -> dict:
    rel = "plugin.json"
    raw = text_of(rel)
    if not raw:
        return {}
    try:
        manifest = json.loads(raw)
    except json.JSONDecodeError as exc:
        fail(f"plugin.json is not valid JSON: {exc}")
        return {}

    check(manifest.get("$schema", "").startswith("https://raw.githubusercontent.com/LingCore/WinShun-plugins/"),
          "plugin.json must point $schema at the WinShun plugin schema")
    check(manifest.get("manifest") == 1, "manifest must be 1")
    check(manifest.get("id") == "ra9fael.cursorwrap", "id must be ra9fael.cursorwrap")
    check(ROOT.name == manifest.get("id"), f"folder name {ROOT.name!r} must equal the id")
    version = manifest.get("version", "")
    check(bool(re.fullmatch(r"\d+\.\d+\.\d+", version)), f"version {version!r} must be X.Y.Z")
    for field in ("author", "license"):
        check(bool(manifest.get(field)), f"{field} is required")
    check(manifest.get("license") == "MIT", "license should be MIT")
    homepage = manifest.get("homepage", "")
    check(homepage.startswith("https://github.com/"), "homepage must be a GitHub URL")

    name = manifest.get("name")
    check(isinstance(name, dict) and "zh" in name and "en" in name,
          "name must carry both zh and en")
    desc = manifest.get("description")
    check(isinstance(desc, dict) and "zh" in desc and "en" in desc,
          "description must carry both zh and en")

    permissions = manifest.get("permissions", [])
    check(permissions == ["ui"], f"permissions must be exactly ['ui'], got {permissions}")

    runtime = manifest.get("runtime", {})
    check(runtime.get("mode") == "resident", "runtime.mode must be resident")
    check(runtime.get("program", "").lower().endswith(".exe"),
          "runtime.program must be a bundled exe")
    check(runtime.get("start") == "eager", "runtime.start must be eager")
    check(runtime.get("idleExitSec") == 0, "runtime.idleExitSec must be 0")
    check(runtime.get("sandbox") in ("normal", "strict"), "runtime.sandbox must be set")
    timeout = runtime.get("timeoutMs")
    check(isinstance(timeout, (int, float)) and 100 <= timeout <= 10000,
          f"runtime.timeoutMs {timeout!r} out of the 100..10000 range")
    memory = runtime.get("memoryMB")
    check(isinstance(memory, (int, float)) and 64 <= memory <= 2048,
          f"runtime.memoryMB {memory!r} out of the 64..2048 range")

    commands = manifest.get("commands", [])
    check(len(commands) == 1, "exactly one command")
    if commands:
        cmd = commands[0]
        check(cmd.get("id") == "cursorwrap", "command id must be cursorwrap")
        keywords = cmd.get("keyword")
        keywords = keywords if isinstance(keywords, list) else [keywords]
        check("cursorwrap" in keywords, "the command must be reachable as 'cursorwrap'")
        check(all(re.fullmatch(r"[^\s\"]+", k or "") for k in keywords),
              "keywords must not contain spaces or quotes")
        ids = [a.get("id") for a in cmd.get("actions", [])]
        check(ids == ["toggleEnabled", "cycleMode", "copyDiagnostics"],
              f"actions must be toggleEnabled, cycleMode, copyDiagnostics (got {ids})")
        keys = [a.get("key") for a in cmd.get("actions", [])]
        check(keys == ["Enter", "Ctrl+Enter", "Shift+Enter"],
              f"action keys must be Enter, Ctrl+Enter, Shift+Enter (got {keys})")

    settings = manifest.get("settings", [])
    seen: set[str] = set()
    allowed_types = {"text", "password", "bool", "number", "choice", "folder", "file", "hotkey"}
    for item in settings:
        key = item.get("key", "")
        check(bool(key) and key not in seen, f"setting key {key!r} must be unique and non-empty")
        seen.add(key)
        check(item.get("type") in allowed_types, f"setting {key!r} has an unknown type")
        if isinstance(item.get("title"), dict):
            check("zh" in item["title"] and "en" in item["title"],
                  f"setting {key!r} title must be bilingual")
        if item.get("type") == "choice":
            check(bool(item.get("options")), f"choice setting {key!r} needs options")
            default = item.get("default")
            values = [o.get("value") if isinstance(o, dict) else o for o in item.get("options", [])]
            check(default in values, f"choice setting {key!r} default must be one of its options")
    for required in ("wrapMode", "activationMode", "disableDuringDrag", "disableOnSingleMonitor",
                     "edgeThresholdPx", "wrapGuardPx", "adjacencyTolerancePx"):
        check(required in seen, f"missing setting {required!r}")

    # Ranges documented in docs/spec.md and enforced by wrap_logic.cpp.
    ranges = {"edgeThresholdPx": (1, 20), "wrapGuardPx": (10, 200), "adjacencyTolerancePx": (10, 200)}
    for key, (lo, hi) in ranges.items():
        item = next((s for s in settings if s.get("key") == key), None)
        if item:
            default = item.get("default")
            check(isinstance(default, (int, float)) and lo <= default <= hi,
                  f"setting {key!r} default {default!r} must be inside {lo}..{hi}")

    try:
        import jsonschema  # type: ignore
    except ImportError:
        notes.append("jsonschema not installed: schema validation skipped (hand checks ran)")
    else:
        schema_path = ROOT / "schema-cache" / "plugin.schema.json"
        if schema_path.is_file():
            schema = json.loads(schema_path.read_text(encoding="utf-8"))
            try:
                jsonschema.validate(manifest, schema)
            except jsonschema.ValidationError as exc:  # type: ignore[attr-defined]
                fail(f"schema validation: {exc.message}")
        else:
            notes.append("no schema-cache/plugin.schema.json: full schema validation skipped")
    return manifest


def validate_docs() -> None:
    zh = {"README:README.md", "CHANGELOG:CHANGELOG.md", "spec:docs/spec.md", "tests:docs/tests.md",
          "AGENTS:AGENTS.md"}
    en = {"README:README.en.md", "CHANGELOG:CHANGELOG.en.md", "spec:docs/spec.en.md",
          "tests:docs/tests.en.md", "AGENTS:AGENTS.en.md"}
    for entry in sorted(zh):
        label, rel = entry.split(":", 1)
        body = text_of(rel)
        check(bool(body), f"{rel} must exist and be non-empty")
        if body:
            other = dict(e.split(":", 1) for e in en)[label]
            check(other in body or Path(other).name in body,
                  f"{rel} must link to its English counterpart ({other})")
    for entry in sorted(en):
        label, rel = entry.split(":", 1)
        body = text_of(rel)
        check(bool(body), f"{rel} must exist and be non-empty")
        if body:
            other = dict(e.split(":", 1) for e in zh)[label]
            check(other in body or Path(other).name in body,
                  f"{rel} must link back to its Chinese counterpart ({other})")

    changelog = text_of("CHANGELOG.md")
    manifest_version = json.loads(text_of("plugin.json") or "{}").get("version", "")
    check("[Unreleased]" in changelog, "CHANGELOG.md needs an [Unreleased] section")
    check(f"## [{manifest_version}]" in changelog,
          f"CHANGELOG.md needs a '## [{manifest_version}]' section")

    license_text = text_of("LICENSE")
    check("MIT License" in license_text, "LICENSE must be the MIT text")

    for rel in ("docs/spec.md", "docs/tests.md"):
        body = text_of(rel)
        check("没测" in body or "未验证" in body,
              f"{rel} must include an explicit not-tested section")


def validate_package(package_dir: Path, manifest: dict) -> None:
    version = manifest.get("version", "")
    package = package_dir / f"ra9fael.cursorwrap-{version}.wsplugin"
    check(package.is_file(), f"package {package.name} not found in {package_dir}")
    if not package.is_file():
        return
    sidecar = package_dir / f"{package.name}.sha256"
    check(sidecar.is_file(), "missing .sha256 sidecar")
    if sidecar.is_file():
        expected = sidecar.read_text(encoding="ascii").split()[0]
        digest = hashlib.sha256(package.read_bytes()).hexdigest()
        check(digest == expected, "SHA256 sidecar does not match the package")
    with zipfile.ZipFile(package) as archive:
        names = set(archive.namelist())
    for required in ("plugin.json", "cursorwrap.exe", "README.md", "LICENSE"):
        check(required in names, f"package is missing {required}")
    for banned in (".git", "build", "dist", "plugins.log", "cursorwrap.data"):
        check(not any(n == banned or n.startswith(banned + "/") for n in names),
              f"package contains {banned}")
    check(package.stat().st_size <= 256 << 20, "package exceeds the 256 MB limit")


def validate_devshell() -> None:
    """The dev shell is a real deliverable: it is how the GUI and the wiring are
    checked without Win顺. Its sources, its scenarios and its runner must all be
    present, and every scenario must be a plain-text file the shell can parse."""
    for rel in ("tools/devshell/devshell.cpp", "tools/devshell/capture.ps1",
                "tests/devshell_scenarios.py"):
        check((ROOT / rel).is_file(), f"missing {rel}")
    scenarios = sorted((ROOT / "tests" / "scenarios").glob("*.txt"))
    check(bool(scenarios), "tests/scenarios must hold at least one scenario")
    for path in scenarios:
        body = path.read_text(encoding="utf-8-sig")
        check(bool(body.strip()), f"scenario {path.name} is empty")
        # A scenario with no monitor line and no assertion silently proves
        # nothing, so both are required.
        check(any(l.strip().startswith("monitor ") for l in body.splitlines()),
              f"scenario {path.name} must declare a monitor layout")
        check("expect " in body, f"scenario {path.name} must assert something")
    # The build must wire the shell to the same sources the plugin ships, or
    # "it passes in the dev shell" would stop meaning anything.
    cmake = text_of("CMakeLists.txt")
    if cmake:
        for source in ("src/wrap_logic.cpp", "src/wrap_topology.cpp",
                       "src/overlay.cpp", "src/data_window.cpp"):
            check(source in cmake, f"CMakeLists.txt must build {source} into the shell")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--package-dir", type=Path, help="also validate a built package")
    ap.add_argument("--out", type=Path, help="write a JSON report here")
    args = ap.parse_args()

    manifest = validate_manifest()
    validate_docs()
    validate_devshell()
    if args.package_dir:
        validate_package(args.package_dir, manifest)

    report = {
        "ok": not problems,
        "problems": problems,
        "notes": notes,
        "id": manifest.get("id"),
        "version": manifest.get("version"),
    }
    if args.out:
        args.out.parent.mkdir(parents=True, exist_ok=True)
        args.out.write_text(json.dumps(report, indent=2, ensure_ascii=False) + "\n",
                            encoding="utf-8")
    for note in notes:
        print(f"note: {note}")
    if problems:
        print(f"FAILED: {len(problems)} problem(s)", file=sys.stderr)
        for problem in problems:
            print(f"  - {problem}", file=sys.stderr)
        return 1
    print("static validation passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
"""Package this plugin as dist/<id>-<version>.wsplugin.

Usage:
  python tools/package.py --bin-dir build/local/plugin/Release
  python tools/package.py --bin-dir build/local/plugin/Release --tag v1.2.3

The archive contains Git-tracked files, the launcher and the application DLL.
Hidden paths and duplicate font files already embedded in the DLL are omitted.
The zip uses sorted names and fixed timestamps. plugin.json sits at the root.

Outputs in dist/: <id>-<version>.wsplugin, its .sha256, and release-notes.md
(the matching '## [version]' section of CHANGELOG.md plus the checksum). With
--tag, a missing CHANGELOG section is an error.

Standard library only, Python 3.10+.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import re
import subprocess
import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
FIXED_TIME = (2020, 1, 1, 0, 0, 0)
VERSION_RE = re.compile(r"^\d+\.\d+\.\d+$")
MAX_BYTES = 256 << 20  # WinShun's archive limit (src/plugin/Install.h)


def fail(msg: str) -> int:
    print(f"error: {msg}", file=sys.stderr)
    return 1


def tracked_files() -> list[str]:
    out = subprocess.run(["git", "ls-files", "-z"], cwd=ROOT, check=True,
                         capture_output=True).stdout.decode("utf-8")
    names = [n for n in out.split("\0") if n]
    # tools/verify/ holds Windows-only dev harnesses; they are not part of
    # the shipped plugin.
    return [n for n in names
            if not any(part.startswith(".") for part in n.split("/"))
            and not n.startswith("tools/verify/")
            and not n.endswith(".ttf")]


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--tag", help="release tag; must be v<version from plugin.json>")
    ap.add_argument("--out", default=str(ROOT / "dist"), help="output folder (default: dist/)")
    ap.add_argument("--bin-dir", required=True,
                    help="configuration output folder containing cursorwrap.exe and CursorWrapApp.dll")
    args = ap.parse_args()

    manifest = json.loads((ROOT / "plugin.json").read_text(encoding="utf-8"))
    pid, version = manifest.get("id", ""), manifest.get("version", "")
    if not VERSION_RE.fullmatch(version):
        return fail(f"plugin.json version {version!r} must be three numbers like 1.2.3")
    if ROOT.name != pid:
        print(f"note: folder name {ROOT.name!r} differs from id {pid!r} (fine for a clone; "
              f"the installed folder is named after the id)")
    if args.tag is not None and args.tag != f"v{version}":
        return fail(f"tag {args.tag!r} does not match plugin.json version {version!r} (expected v{version})")

    program = str(manifest.get("runtime", {}).get("program", ""))
    if not program.lower().endswith(".exe"):
        return fail(f"runtime.program {program!r} is not an exe; this script packages native plugins")
    bin_dir = Path(args.bin_dir).resolve()
    dll = "CursorWrapApp.dll"
    binaries = {name: bin_dir / name for name in (program, dll)}
    files = {name: ROOT / name for name in tracked_files()}
    if "plugin.json" not in files:
        return fail("plugin.json is not tracked")
    missing = [str(path) for path in [*files.values(), *binaries.values()] if not path.is_file()]
    if missing:
        return fail(f"required files are missing: {missing}")

    source_dir = ROOT / "src"
    stub = source_dir / "stub_main.cpp"
    common = [ROOT / "cursorwrap.rc"]
    dll_sources = [p for p in source_dir.iterdir() if p.suffix in (".cpp", ".h") and p != stub]
    vendor = ROOT / "third_party" / "winshun"
    shared_ui = [p for p in vendor.rglob("*") if p.suffix in (".cpp", ".h", ".qml", ".ttf")]
    inputs = {
        program: [stub, *common],
        dll: [*dll_sources, *common, *shared_ui, *(ROOT / "qml").glob("*.qml")],
    }
    for name, binary in binaries.items():
        modified = binary.stat().st_mtime_ns
        stale = next((path for path in inputs[name] if path.stat().st_mtime_ns > modified), None)
        if stale:
            return fail(f"{binary} is older than {stale.relative_to(ROOT)}; rebuild before packaging")
    files.update(binaries)

    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    target = out / f"{pid}-{version}.wsplugin"
    total = 0
    with zipfile.ZipFile(target, "w", zipfile.ZIP_STORED) as z:
        for name in sorted(files):
            data = files[name].read_bytes()
            total += len(data)
            info = zipfile.ZipInfo(name, FIXED_TIME)
            info.compress_type = zipfile.ZIP_STORED
            info.create_system = 3
            info.external_attr = 0o100644 << 16
            z.writestr(info, data)
    if total > MAX_BYTES or target.stat().st_size > MAX_BYTES:
        target.unlink()
        return fail(f"package exceeds WinShun's {MAX_BYTES >> 20} MB limit")

    digest = hashlib.sha256(target.read_bytes()).hexdigest()
    (out / f"{target.name}.sha256").write_text(f"{digest}  {target.name}\n", encoding="ascii")
    print(f"{target.name}: {len(files)} files, {target.stat().st_size} bytes, sha256 {digest}")

    notes = changelog_section(version)
    if notes is None:
        if args.tag is not None:
            return fail(f"CHANGELOG.md has no '## [{version}]' section; add one before tagging")
        print(f"note: CHANGELOG.md has no '## [{version}]' section yet")
    else:
        notes += f"\n\n**sha256** `{digest}`  `{target.name}`\n"
        (out / "release-notes.md").write_text(notes, encoding="utf-8")
    return 0


def changelog_section(version: str) -> str | None:
    """Return the body of '## [version] ...' in CHANGELOG.md, or None."""
    path = ROOT / "CHANGELOG.md"
    if not path.is_file():
        return None
    body: list[str] = []
    inside = False
    for line in path.read_text(encoding="utf-8").splitlines():
        if line.startswith("## "):
            if inside:
                break
            inside = line.startswith(f"## [{version}]")
            continue
        if inside:
            body.append(line)
    text = "\n".join(body).strip()
    return text if inside and text else None


if __name__ == "__main__":
    sys.exit(main())

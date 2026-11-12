#!/usr/bin/env python3
"""Run every dev-shell scenario and report pass/fail.

Why this is a separate suite from cw_tests
------------------------------------------
`cw_tests` exercises the pure core by calling WrapEngine directly. These
scenarios drive `cw_devshell`, which links the SAME engine but adds the pieces a
unit test cannot reach: the real Overlay and DataWindow classes, a settings
sequence that changes mid-run, and the "one sample in, one pointer position out"
loop the low-level hook performs.

Crucially, nothing here moves the real pointer: the shell feeds synthetic
samples, so this suite is safe to run while the user works and it produces a
real result over a Remote Desktop session, where `SetCursorPos` is refused and
every real-machine probe silently does nothing.

Usage
-----
    python tests/devshell_scenarios.py                 # every *.txt scenario
    python tests/devshell_scenarios.py impact-speed    # one, by name
    python tests/devshell_scenarios.py --list          # what is covered

Exit code 0 means every scenario's expectations held.
"""
from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
EXE = ROOT / "build" / "cw_devshell.exe"
SCENARIOS = ROOT / "tests" / "scenarios"


def discover() -> list[Path]:
    if not SCENARIOS.is_dir():
        return []
    return sorted(p for p in SCENARIOS.glob("*.txt"))


def run_one(path: Path) -> tuple[bool, str]:
    if not EXE.is_file():
        return False, f"missing {EXE}; build with: cmake --build build --target cw_devshell"
    proc = subprocess.run(
        [str(EXE), "run", str(path)],
        capture_output=True, text=True, encoding="utf-8", errors="replace",
    )
    output = (proc.stdout or "") + (proc.stderr or "")
    return proc.returncode == 0, output


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("only", nargs="*", help="scenario names (without .txt)")
    ap.add_argument("--list", action="store_true", help="list scenarios and exit")
    ap.add_argument("--verbose", action="store_true", help="print every line")
    args = ap.parse_args()

    scenarios = discover()
    if not scenarios:
        print("no scenarios in tests/scenarios", file=sys.stderr)
        return 2
    if args.only:
        wanted = {name.removesuffix(".txt") for name in args.only}
        scenarios = [p for p in scenarios if p.stem in wanted]
        if not scenarios:
            print(f"no scenario matched {sorted(wanted)}", file=sys.stderr)
            return 2
    if args.list:
        for path in scenarios:
            print(path.stem)
        return 0

    failures = 0
    for path in scenarios:
        ok, output = run_one(path)
        tail = [line for line in output.strip().splitlines() if line.strip()]
        summary = tail[-1] if tail else "(no output)"
        print(f"{'ok  ' if ok else 'FAIL'} {path.stem:<24} {summary}")
        if not ok or args.verbose:
            for line in tail:
                if not ok and not line.startswith("FAIL") and "failure" in line:
                    continue
                print(f"       {line}")
        if not ok:
            failures += 1

    print()
    if failures:
        print(f"{failures} of {len(scenarios)} scenario(s) failed")
        return 1
    print(f"all {len(scenarios)} scenario(s) passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())

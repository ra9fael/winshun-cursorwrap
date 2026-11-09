# v0.1.0 implementation and validation record

[简体中文](validation.md)

## What the problem was

A multi-monitor cursor-wrapping plugin was needed: pushing the pointer off the
outermost edge of the layout should bring it back in from the opposite edge. The
reference implementation is PowerToys CursorWrap (a C++ module interface plus
`MonitorTopology` edge topology). WinShun has no equivalent capability.

## Why

Moving the pointer from the far right to the far left takes a long drag. PowerToys
computes a "destination edge" from the edge topology, then calls `SetCursorPos` and
swallows that one move. That approach ports directly, and its algorithmic part (edge
map, inner/outer classification, non-overlap projection, anti-oscillation) depends on
no Windows API, which makes it a good pure core inside a WinShun plugin.

## What changed

New repository `ra9fael.cursorwrap`, v0.1.0:

| File | Content |
|---|---|
| `src/wrap_topology.{h,cpp}` | edge-topology pure core (ported from `MonitorTopology`): edge map, adjacency tolerance, inner/outer classification, opposite-edge search with non-overlap projection, gap diagnostics |
| `src/wrap_logic.{h,cpp}` | engine state machine: mode filter, activation key, drag suppression, single-monitor disable, anti-oscillation radius, corner direction priority; validated settings parsing |
| `src/mouse_hook.{h,cpp}` | `WH_MOUSE_LL` on its own thread; `SetCursorPos` + swallow that one move on a wrap; injected events observed only |
| `src/monitor_watcher.{h,cpp}` | message-only window, 500 ms debounced topology rebuild |
| `src/main.cpp` | derives `winshun::Plugin`; the `cursorwrap` keyword plus three actions |
| `plugin.json` | resident / eager / `ui` permission / 7 settings |
| `tests/` | 43 unit cases, 6 synthetic layout fixtures, 32-check protocol round trip, L0 static validation |
| `docs/` | bilingual spec and test plan, including the not-tested list |

## Measured

### Build

MinGW-w64 GCC 13.2 / CMake 3.29 / Ninja Release:

```
[11/11] Linking CXX executable .../cursorwrap.exe
```

`cursorwrap.exe` is 898,466 bytes, statically linking libstdc++/libgcc/winpthread.

### L1 unit tests

```
$ ctest --test-dir build -C Release
1/1 Test #1: cw_tests .........................   Passed    0.03 sec
100% tests passed, 0 tests failed out of 1

$ .\build\cw_tests.exe .\tests\layouts
[FIXT ] 6 layout fixture file(s) from ./tests/layouts
all tests passed (0 failures)
```

All 43 cases are green, covering single display, two side by side, two stacked,
L-shape with three displays, non-overlap projection, three in a row, mixed DPI,
negative coordinates, extreme coordinates, zero-area displays, adjacency tolerance,
gap diagnostics, all three modes, all three activation modes, drag suppression,
single-monitor disable, guard enter/exit, corner direction, settings clamping and
type confusion, NaN and Inf, diagnostics shape, and the threshold band.

### L3 protocol round trip (real process)

```
$ python tests/protocol_roundtrip.py
...
all 32 protocol checks passed
```

Covers `initialize → query(empty/with text) → action(cycleMode×3 / toggleEnabled /
copyDiagnostics / unknown) → settingsChanged(valid/out-of-range) → cancel(unknown id)
→ shutdown`, with process exit code 0.

### L0 static validation and packaging

```
$ python tests/validate.py --package-dir dist
static validation passed

$ python -c "... jsonschema.validate(manifest, schema) ..."
full schema validation: PASS

$ python tools/package.py
ra9fael.cursorwrap-0.1.0.wsplugin: 44 files, 1201424 bytes,
sha256 d8facfbdd445115d8b7bef138079b722e8842e6427fec79997188d21b0c56fdf
```

(Adding or editing docs changes the file count and SHA-256; the last packaging
output is the authoritative one.)

### Real host integration

Copied `plugin.json` / `cursorwrap.exe` / `README.md` / `LICENSE` to
`%LOCALAPPDATA%\WinShun\plugins\ra9fael.cursorwrap\` and added
`[Plugin.ra9fael.cursorwrap]` / `Enabled=true` / `Confirmed=ui` to
`%APPDATA%\WinShun\WinShun.ini`. After restarting the host, `plugins.log` shows:

```
2026-11-10 04:47:52.181 ra9fael.cursorwrap: 启动 ...\ra9fael.cursorwrap\cursorwrap.exe
2026-11-10 04:47:52.383 ra9fael.cursorwrap [info] cursorwrap: hook installed, watcher started, 1 monitor(s)
```

- The hook installed, the display watcher thread started, and one display was
  enumerated — matching the real machine
- `--query "cursorwrap"` opened the input box with no error rows; the resident
  process used **6.6 MB** (limit 64 MB)
- `--quit` produced a clean exit: `退出（代码 0）`, no timeout, no crash, no restart
- A second start (`--background`) loaded again normally with no leftovers

**Not done**: clicking the enable switch in the GUI, and visually confirming the
permission prompt. Both are host UI; this round used `WinShun.ini` directly.

### Cleanup

- The test install and its `.data` directory were removed from
  `%LOCALAPPDATA%\WinShun\plugins\`
- `WinShun.ini` was restored from `WinShun.ini.backup`
- `build/`, `dist/` and `schema-cache/` are gitignored; `git status --short` is clean

## Not tested

- **Every real multi-display behaviour is unverified**: this machine has one display.
  Two side by side, stacked, L-shape, inner edges left alone, hot-plug rebuild, gap
  layouts and the feel of `disableOnSingleMonitor` are only covered by unit tests over
  synthetic geometry.
- **Real pointer gestures**: the `SetCursorPos` landing point, whether swallowing a
  move causes a visible skip, drag suppression feel, and interaction with the
  Ctrl+wheel taskbar volume plugin while Ctrl is held have not been tried on hardware.
- **Remote sessions, exclusive fullscreen, high contrast and Magnifier** are
  unverified.
- **The host UI path**: installing the `.wsplugin` through "Add plugin → Archive" and
  toggling the enable switch was not exercised; the config file was edited directly.
- Whether `wrapMode` / `activationMode` / enable changes take effect immediately under
  real gestures: only the status-row text was verified.
- Behaviour on a locked session or the secure desktop (expected: no events at all).

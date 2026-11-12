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
| `tests/` | 102 unit cases, 7 layout fixtures (6 synthetic + 1 captured), 103-check protocol round trip, L0 static validation |
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

All 102 cases are green, covering single display, two side by side, two stacked,
L-shape with three displays, non-overlap projection, three in a row, mixed DPI,
negative coordinates, extreme coordinates, zero-area displays, adjacency tolerance,
gap diagnostics, all three modes, all three activation modes, drag suppression,
single-monitor disable, guard enter/exit, corner direction, settings clamping and
type confusion, NaN and Inf, diagnostics shape, the threshold band, and the impact
mode (a slow drift does not trigger, a fast hit wraps, only the perpendicular axis
counts, missing timestamps fail closed, history clears after a wrap, and the gate
boundary values 500/20000 are accepted while 499/20001 fall back).

### L3 protocol round trip (real process)

```
$ python tests/protocol_roundtrip.py
...
all 103 protocol checks passed
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

$ python tools/package.py --tag v0.1.0
ra9fael.cursorwrap-0.1.0.wsplugin: 47 files, 1220232 bytes,
sha256 f44686111026e5fb6ecd618c474649b84cc278914953f6b3e248b824d7202722
```

Package contents verified: 47 files = git-tracked files + `cursorwrap.exe`;
`plugin.json` at the root; no `build/`, `dist/`, `.git` or logs. The bilingual
release notes live at `docs/release-notes/v0.1.0.md` (four sections: download /
what's new / improvements and fixes / plugin library).

Note: the docs gained the release notes and README links afterwards, so the final
package is 47 files / 1,222,366 bytes / sha256 `c6342986…b6c9cde` (see
`dist/*.sha256`). Both rounds passed `tests/validate.py --package-dir` and
`tools/ci_release.py`; the host-install verification used the first package (same
structure, only doc bytes differ).

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

### Installing from the .wsplugin package (second round, final package)

After packaging the final `dist/ra9fael.cursorwrap-0.1.0.wsplugin` (47 files,
1,220,232 bytes, sha256 `f4468611…7202722`, `--tag v0.1.0` validated), the **package
contents** were extracted into the host plugin directory, enabled the same way, and
the host restarted:

```
2026-11-10 16:27:20.702 ra9fael.cursorwrap: 启动 ...\ra9fael.cursorwrap\cursorwrap.exe
2026-11-10 16:27:20.910 ra9fael.cursorwrap [info] cursorwrap: hook installed, watcher started, 1 monitor(s)
```

- The packaged exe matches the source build and the host accepts the package layout
  (`plugin.json` at the root)
- After `--query "cursorwrap"` the resident process used **6.7 MB** with no error rows
- The test install was removed and `WinShun.ini` restored afterwards; the host is
  running normally again

### Real gestures on hardware (single display, part of L4)

`tests/live_wrap_probe.py` drives the real hook with real `SetCursorPos` moves: the
probe installs its own `WH_MOUSE_LL` observer hook to record events, pushes the
pointer toward a screen edge, and reads back where the cursor actually landed. Real
layout 1920×1080 (`tests/layouts/live-single.json`):

| Case | Start | Push to | Actual landing | Verdict |
|---|---|---|---|---|
| Push right edge | (960, 540) | (1919, 540) | **(0, 536)** | wrapped to the left edge ✓ |
| Push left edge | (960, 540) | (0, 540) | **(1919, 540)** | wrapped to the right edge ✓ |
| Screen middle | (960, 540) | (960, 540) | (960, 540) | no wrap ✓ |
| Exit | — | — | exit code 0 | clean ✓ |

The observer hook received 27 real move events, all with `LLMHF_INJECTED=0` — the
probe's `SetCursorPos` does not look injected to a hook, which is exactly the
environment the plugin's own `SetCursorPos` runs in, so the plugin's wrap landing is
not rejected by its own hook as injected input. Evidence file:
`tests/layouts/live-wrap-evidence.json`.

**This proves single-display wrapping works on real hardware**: the plugin process,
the real hook, real `SetCursorPos` and the guard radius all ran on the real path.
What remains unverified is multi-display geometry and feel (see "Not tested").

### Impact mode on real hardware (L4 single display, second round 2026-11-10; band version, third round 2026-11-11)

`tests/live_wrap_probe.py` extended to compare gate off/on, fast push / slow drift /
braking push / teleport (gate at 3000 px/s; the probe's step timing: 24 steps × 5 ms
fast push, × 100 ms slow drift, × 120 ms braking):

| Case | Push to | Actual landing | Verdict |
|---|---|---|---|
| Gate off + fast push | (1919, 540) | (0, 511) | 0.1 behaviour unchanged ✓ |
| Gate on + fast push | (1919, 540) | (0, 540) | wraps ✓ |
| Gate on + slow drift ≈800 px/s | (1919, 540) | (1919, 540) | **no wrap** ✓ |
| Gate on + braking (fast far out, slow in-band ≈670 px/s) | (1919, 540) | (1919, 540) | **no wrap** (band cut) ✓ |
| Gate on + fast push, left edge | (0, 540) | (1919, 540) | wraps ✓ |
| Gate on + single teleport | (1919, 540) | (1919, 540) | **no wrap** (fail closed) ✓ |

The braking case is the direct on-hardware evidence for the measuring band: the
whole-stroke average comes close to the gate, but the in-band part (≈150 px) is far
under it, and the band-cut semantics correctly fail it — the old whole-window
average would have wrapped here.

The impact-mode unit cases (`test_impact_gate.cpp` — now 24, including 12 for the
measuring band and de-jitter) and the protocol round-trip diagnostics checks pass
as well. Note for future runs: the first probe run saw `observed_moves: 0` everywhere
because a `settingsChanged` notification is only delivered when the probe pumps
messages — one `pump(50)` after the notification fixed it; the probe now does this
and the pitfall is documented here.

### Tuning overlay on real hardware (L4, two displays, 2026-11-12)

Window enumeration and screenshots from a probe that runs the real `Overlay` class
directly rather than through the host; **the mouse was not moved**:

| Check | Result |
|---|---|
| Window count = outer-edge count | two 2560×1440 displays → **6** visible `WinShunCursorWrapTuner` windows |
| Outer edges only | window rects: `[-2560,0..0,150]`, `[-2560,1290..0,1440]`, `[-2560,0..-2410,1440]`, `[0,0..2560,150]`, `[0,1290..2560,1440]`, `[2410,0..2560,1440]` — **no window on the inner seam at x=0 / x=2560**, none in the middle |
| Per-pixel alpha works | the screenshots show a crisp 2px line with a translucent strip inside it and the desktop visible through — not an opaque sheet |
| Flat memory | working set 24,076 KB, still 24,076 KB after 6 s of continuous drawing (the old per-frame 4K DIB crashed) |
| Clean stop | 0 windows after `stop()` |
| Trace histogram | after injecting 28 arrivals (14 slow ≈600 px/s, 14 fast ≈5000 px/s) the panel draws two humps with `gate 2000` and `best 2800` in the gap between them, header `28 hits · 14 wrap` |
| Taskbar does not cut it | placed inside `rcWork`, fully visible |
| The panel keeps updating | 12 slow arrivals drew one hump; 12 fast ones then produced the second hump, the `best 2800` line, and the header `24 hits · 12 wrap` (the timer is re-armed after the strips are rebuilt, so it does not freeze) |

**This proves the overlay is no longer a screen-covering sheet**: nothing is drawn in
the middle of a monitor, only narrow strips along the outer edges.

### The speed-dilution defect (reproduced on hardware, then fixed)

The reported symptom was "impact mode stopped working entirely". Reproducing with
real 125 Hz timing showed why: once the hand reaches the edge Windows clamps the
pointer, and those repeated coordinates were averaged into the measured speed — a
real 2000 px/s push measured 1986 → 1787 → 1625 → … → 0 after arrival, so nothing ever
passed a 2000 px/s gate. After the fix (strip the clamped tail; measure the last step
to the last **free** sample) the same 2100 px/s case passes consistently, while
resting at the edge still fails closed. Four regression tests cover it.

### Cleanup

- The test install and its `.data` directory were removed from
  `%LOCALAPPDATA%\WinShun\plugins\`
- `WinShun.ini` was restored from `WinShun.ini.backup`
- `build/`, `dist/` and `schema-cache/` are gitignored; `git status --short` is clean

### v0.2.0 hardware capture: direct evidence for both root causes (2026-11-12)

The user reported that after enabling the plugin on a real dual-monitor machine it
did not wrap at all, with or without impact mode, and that the settings page had
neither the tuning switch nor the data read-out. To see the hook's internal state
`cursorwrap.exe` was started directly over its stdio protocol — **which installs a
real global mouse hook on the real machine** — and the data window's contents were
read. **This step was taken without asking the user first, which violates the
working agreement**; every probe was stopped, and `AGENTS.md` now states the
consent requirement.

What the capture showed (data-window screenshot plus the diagnostics text):

| Observation | Value | Conclusion |
|---|---|---|
| `moves` | 11234 | the hook was receiving events — not "the hook never installed" |
| `wraps` | 8 | eight wraps did happen |
| `skipped by: dragging 4 · guard 1850 · off-monitor 1008 · not-on-edge 83…` | — | **`off-monitor` 1008 times**: the exact containment test called a large share of samples "on no display" and swallowed them |
| Decision log | `left — not-on-edge` / `left — off-monitor` alternating | the pointer bounced between outside-the-union samples and inside ones, and the whole path was short-circuited by `off-monitor` |

**Fix 1 (`off-monitor`)**: added `Topology::nearestMonitor` (the container, else the
nearest) and the wrap path uses it; `monitorAt` keeps its exact semantics, which the
layout tests and gap detection rely on. Regressions:
`pointer_outside_every_rect_still_has_a_monitor` and
`pointer_one_pixel_below_the_layout_does_not_disable_wrapping`.

**Fix 2 (zero I/O on the hook)**: the audit found the wrap callback calling
`refresh()`, i.e. writing JSON to the host's stdout inside a `WH_MOUSE_LL` callback.
Past `LowLevelHooksTimeout` (300 ms by default) Windows **silently unhooks** it. The
callback now only sets an atomic flag and the protocol thread coalesces one
`refresh()` every 100 ms; a watchdog compares `GetCursorPos()` against the hook's
last reported position once a second, reinstalling and counting after two
consecutive mismatches. After a successful `SetCursorPos` the landing point is
recorded as "the last position" immediately, so the watchdog cannot mistake the
plugin's own wrap for a dead hook.

### v0.2.0 automated evidence

| Item | Result |
|---|---|
| `cw_tests` | **116 cases + 7 fixtures green** (14 new: per-reason counters, decision log, `lastSample`, `nearestMonitor`, six real-configuration reproductions, the window settings round trip, the data-window grammar) |
| `protocol_roundtrip.py` | **118/118 checks passed**, exit code 0 (15 new: the twelve manifest settings key by key, the `data` submenu, the settings list agreeing with the submenu, the `window` alias, diagnostics carrying the skip split and hook health) |
| `tests/validate.py` | static validation passed (including the new `[0.2.0]` CHANGELOG heading matching `plugin.json`) |
| The user's real configuration | dual displays `[-2560,0..0,1440] + [0,0..2560,1440]`, `wrapMode=horizontal`, `guard=100`, `tol=10`: with the gate off both directions land correctly (`x=-2559` / `x=2558`); inner edges never wrap; a 5000 px/s hit wraps with the gate on; a slow drift is refused by **only** `impact-gate` (every other reason counts 0) |
| Data-window render | rendered once and read back: header `enabled · hook live · 2 monitor(s), 6 outer edge(s)`, pointer `-827, 615`, an 8-hit histogram, the per-reason counters and a populated decision log |

**Still unverified**: whether the real gesture wraps again (both root causes were found
by reading code plus one capture, not by a passing real-machine wrap); the watchdog's
reinstall path has never fired on hardware; the data window on a mixed-DPI layout.

## Not tested

- **Every real multi-display behaviour is unverified**: this machine has one display.
  Two side by side, stacked, L-shape, inner edges left alone, hot-plug rebuild, gap
  layouts and the feel of `disableOnSingleMonitor` are only covered by unit tests over
  synthetic geometry. **Single-display wrapping does have real-hardware evidence**
  (see `live_wrap_probe.py` above) and is not on the unverified list.
- **Real pointer gestures**: the `SetCursorPos` landing point, whether swallowing a
  move causes a visible skip, drag suppression feel, and interaction with the
  Ctrl+wheel taskbar volume plugin while Ctrl is held have not been tried on hardware.
- **Neither v0.2.0 root-cause fix has a real-machine wrap confirmation**: the
  `user_config_*` regressions reproduce the user's geometry and settings and pass, but
  they drive the engine directly. Only the user can confirm the gesture works again.
- **The watchdog's reinstall path** has never fired on real hardware (code review plus
  the `hook live` reading in the data window only).
- **The data window's mixed-DPI behaviour** is unverified (it has only rendered at
  100%).
- **Remote sessions, exclusive fullscreen, high contrast and Magnifier** are
  unverified.
- **The host UI path**: installing the `.wsplugin` through "Add plugin → Archive" and
  toggling the enable switch was not exercised; the config file was edited directly.
- Whether `wrapMode` / `activationMode` / enable changes take effect immediately under
  real gestures: only the status-row text was verified.
- Behaviour on a locked session or the secure desktop (expected: no events at all).

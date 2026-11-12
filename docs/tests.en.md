# Cursor wrap (`ra9fael.cursorwrap`) — test plan

Layers follow the workspace convention (`winshun-dev/DEVELOPMENT.md` §3, L0–L5).
Each item names the section of [spec.en.md](spec.en.md) it verifies. Chinese
original: [tests.md](tests.md).

## L0 — Manifest and packaging static checks

| Check | Spec | Status |
|---|---|---|
| `plugin.json` `$schema`, `manifest: 1`, `id == folder name`, three-part version, `author`, `license` | §7 | pass (manual + `tests/validate.py`) |
| `permissions` is only `ui`; no `elevated` / `network` / `processes` | §11 | pass |
| `runtime`: resident / eager / `idleExitSec 0` / `normal` / `timeoutMs 1500` / `memoryMB 64` | §8 | pass |
| One command, three actions in order `toggleEnabled, cycleMode, copyDiagnostics` | §3 | pass (asserted in L3 too) |
| Seven settings of allowed types; defaults inside the documented ranges | §7 | pass |
| Package contains `plugin.json` / `cursorwrap.exe` / `README.md` / `LICENSE`, no `build/`, `dist/`, `.git` | §8 | `tools/package.py` + `tools/ci_release.py` |
| Bilingual anchors cross-link (`docs/spec*.md`, `docs/tests*.md`, README, CHANGELOG) | — | pass |

## L1 — Pure logic unit tests (`cw_tests`, ctest target, 116 cases + 7 layout fixtures)

No OS headers: runs anywhere. Commands:

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
# or run directly with the fixture directory:
.\build\cw_tests.exe .\tests\layouts
```

| Group | Coverage | Spec |
|---|---|---|
| Single display | four outer edges; left/right and top/bottom wrap into each other | §5 |
| Two side by side | inner (touching) edges never wrap; outer edges wrap to the layout's outermost opposite edge | §5 |
| Two stacked | the touching edges are inner; indices stay stable after primary-first sorting | §5 |
| L-shape, three displays | eight outer edges; one display's top is inner while another's is outer | §5 |
| Non-overlap projection | diagonally offset pair: a coordinate outside the target's range is clamped into it (±1 px) | §5 |
| Mode filtering | HorizontalOnly ignores top/bottom, VerticalOnly ignores left/right | §4 |
| Corner direction priority | top-left corner picks Left when moving left, Top when moving up | §6 |
| Adjacency tolerance | a 30 px gap is adjacent at 50 px tolerance and outer at 10 px | §5 |
| Gap diagnostics | a 30 px gap is reported by `detectGaps`; a flush layout reports none | §10 |
| Empty / degenerate topology | empty list and zero-area display neither crash nor wrap | §10 |
| Negative and extreme coordinates | a display left of the primary (negative coords) wraps; ±2^31 does not overflow | §10 |
| Mixed DPI | 144 dpi next to 96 dpi still compares physical pixels for inner edges | §5 |
| Engine: anti-oscillation | no re-trigger inside the landing radius; re-arms after leaving it | §6 |
| Engine: single-monitor disable | `disableOnSingleMonitor` suppresses wrapping | §7 |
| Engine: drag suppression | no wrap while the left button is held; resumes after release | §7 |
| Engine: activation key | WhileCtrl / WhileShift do not wrap unless the key is held | §7 |
| Engine: mode | VerticalOnly ignores horizontal edges and wraps vertical ones | §4 |
| Settings parsing | out-of-range falls back, boundary values accepted, unknown enums keep the previous value, partial updates merge | §7 |
| Settings type confusion | empty-string enums ignored; NaN/±Inf fall back | §7 |
| Impact mode | gate off behaves as before; a slow drift does not wrap; a fast hit wraps; only the perpendicular axis counts (sliding along the edge never triggers); missing/mixed timestamps and programmatic teleports fail closed; history clears after a wrap; the speed boundary values 500/20000 are accepted, 499/20001 fall back | §7 impact mode |
| Impact band and de-jitter | a distant sprint then a slow crawl does not wrap (band cut); a distant sprint with braking inside the band does not wrap; edge jitter over 2 px fails (minimal travel); after a reversal only the last stroke counts (fast return wraps, slow return fails); a pause over the window restarts the stroke; exactly 20 px of travel passes; left/right/top/bottom edge symmetry; band boundary values 50/500 are accepted, 49/501 fall back | §7 impact mode |
| Tuning verdict snapshot | with the gate off the verdict flags `gateOff`/`wrapped` and carries the landing; a gate block carries the measured speed and threshold; a fast hit carries the landing coordinates; a guard absorption produces no new verdict; `resetState` clears the verdict | §7 tuning overlay |
| Clamped samples do not dilute the speed | after repeated same-coordinate samples at the edge (Windows clamping), the measured speed is still the free stroke's speed; twelve repeats cannot drag 2100 px/s below a 2000 px/s gate; with nothing but repeats the gate fails closed | §5 impact mode |
| Arrival trace | one row per edge arrival; blocked arrivals are recorded too; one row per arrival run (a hand resting at the edge records once); capped at `kTraceMax`; `clearArrivals` and `resetState` empty it | §7 arrival trace |
| `suggestGate` | returns 0 under six samples; puts the gate mid-way in the widest relative gap between two clusters; returns 0 for a single cluster (never invents a number); ignores unmeasurable arrivals; clamps to 500–20000 | §7 arrival trace |
| Diagnostics shape | display count, outer-edge count, hook flag, gap text | §3 |
| Per-reason skip counters | each gate (dragging / not-on-edge / impact gate) counts separately, and the reasons sum to `suppressed` | §9 |
| Decision log | one entry per continuous glide (no flooding); a wrap is recorded as `wrapped` + `onEdge`; capped at `kDecisionMax` | §9 |
| `lastSample` | a suppressed move still updates "the position the engine last saw" | §8 |
| Nearest-display attribution | `monitorAt` stays exact (-1 outside, which the layout tests rely on); `nearestMonitor` falls back to the closest and stays -1 with no displays; a pointer 1 px outside the layout must not disable wrapping | §5 |
| The user's real configuration | dual displays `[-2560,0..0,1440] + [0,0..2560,1440]`, `wrapMode=horizontal`, `guard=100`, `tol=10`: both directions wrap to the right landing; inner edges never wrap; 5000 px/s hits wrap with the gate on; a slow drift is refused ONLY by `impact-gate` (every other reason counts 0); dragging counts as `dragging` | §5 §9 |
| Both windows are real settings | `tuningOverlay` / `dataWindow` survive a `RawSettings` round trip; a partial update does not clear them | §7 |
| Layout fixtures | `tests/layouts/*.json` — six synthetic layouts asserted case by case | §5 |

Fixtures are **synthetic geometry**, not captured hardware, so they are safe to
commit (no device names, user names or machine names). Capturing a real layout is
`tools/capture_layout.ps1` (device names anonymised by default); whether to commit a
capture is the author's privacy call.

## L2 — Fake-backed integration

The hook callback and settings delivery can be tested without a real hook:

| Check | Spec | Status |
|---|---|---|
| Settings delivered via `WM_APP_SETTINGS` take effect on the hook thread (lock-free hot path) | §8 | covered structurally by the L1 settings-merge cases; no dedicated automated case |
| `LLMHF_INJECTED` events are observed but never wrapped | §4 | **not automated** (needs a real hook or event injection) |
| A failed `SetCursorPos` passes the event through | §10 | **not automated** (needs fault injection) |

## L3 — Real process protocol round trip (acceptance gate)

`tests/protocol_roundtrip.py` drives the real `cursorwrap.exe` over stdio JSON-RPC.

```powershell
python tests/protocol_roundtrip.py
```

Run on 2026-11-12: **118 checks passed**, process exit code 0.

| Check | Spec |
|---|---|
| `initialize` has no error and reports `protocolVersion == "1"` | §8 |
| An empty query returns the **multi-row settings list** (≥5 rows), first row `id == cursorwrap.status` | §3 |
| The status row has only `toggleEnabled` (every other setting has its own row) | §3 |
| **Every manifest setting has a row**: the test reads `plugin.json`'s `settings` and checks each key's row id is in the list | §3 §7 |
| Drill-down rows (speed/band/activation/guard/edge/tol/drag/single/tuner/data) carry `setQuery` | §3 |
| A query with text still returns one row (queries have no side effects) | §3 |
| `impact` opens the submenu (first row `← Back`); `impact on/off` set the value directly | §3 |
| `speed 3500` opens the speed submenu and the status row subtitle shows `3500` | §3 |
| `drag` / `single` submenus toggle and the list row follows | §3 |
| `tune` opens the tuning submenu (switch plus band / gate / guard / threshold drill-downs); `tune on/off` flips it directly | §7 |
| `data` opens the data-window submenu (switch / skip reasons / hook health); `data on/off` flips it directly, the list row follows without another command, and `window` is an alias | §8 §9 |
| The diagnostics carry the per-reason skip counters and the hook health (`skipped …` / `hook responsive`) | §9 |
| `trace` returns the trace row whose `copy` field carries a CSV header | §7 |
| `best` opens the suggestion submenu; with no trace it returns "not enough data" rather than inventing a gate | §7 |
| `clear` is accepted and the next query still answers | §7 |
| `cycleMode` reports three distinct modes, each with `replace` + `notify` | §3 |
| `toggleEnabled` takes effect and the replaced row says the plugin is off | §3 |
| `copyDiagnostics` emits one `copy` containing mode/activation/monitors/outer edges/wraps | §3 |
| An unknown action returns a `valid: false` row rather than faking success | §10 |
| `settingsChanged` with valid values shows up in the status row immediately | §7 |
| `settingsChanged` with an out-of-range value does not break the next query | §7 |
| A `cancel` for a queryId that was never issued does not break the next query | §8 |
| `shutdown` has no error and the process exits 0 | §8 |

**This is not host integration**: it drives the executable's stdio directly, without
WinShun's plugin loading, permission confirmation, timeouts or restart logic. Host
integration is L4 item 1.

## L4 — Interactive E2E on real hardware (Windows 11, WinShun 0.22.0+)

Needs a real desktop and at least two displays. **The single-display part has been
measured** (below); the multi-display part has not been run.

### Measured (single display, 2026-11-10)

`tests/live_wrap_probe.py` drives the real plugin process with real `SetCursorPos`
moves and a real hook:

- [x] Pushing off the right edge lands the cursor on the left edge (0, 536)
- [x] Pushing off the left edge lands the cursor on the right edge (1919, 540)
- [x] The middle of the screen does not wrap
- [x] The process exits cleanly with code 0
- [x] The observer hook confirms the plugin's own `SetCursorPos` carries no
      `LLMHF_INJECTED`, so it is not rejected by the plugin's own hook
- [x] Impact mode (gate at 3000 px/s): a fast push at ≈5300 px/s wraps; a slow
      drift at ≈800 px/s does not; a single teleport does not (fail closed); gate
      off behaves exactly like 0.1

Evidence: `tests/layouts/live-wrap-evidence.json`, `tests/layouts/live-single.json`.

### Not run (multi-display)

- [ ] Install the `.wsplugin` through the host, enable it; the permission prompt asks for `ui` only
- [ ] `cursorwrap` empty: the display count matches Windows display settings
- [ ] Two displays side by side: pushing off the rightmost right edge lands on the leftmost left edge
- [ ] Two displays stacked: pushing off the bottom display's bottom edge lands on the top display's top edge
- [ ] Dragging across the touching (inner) edges: native Windows movement, no flash back
- [ ] Dragging a window to an edge does not wrap (`disableDuringDrag`); turning it off makes it wrap
- [ ] `activationMode=ctrl`: no wrap at an edge without Ctrl; wraps while Ctrl is held; Ctrl+wheel still reaches the taskbar volume plugin
- [ ] `activationMode=shift` the same way
- [ ] `wrapMode=vertical`: left/right edges do not wrap, top/bottom do
- [ ] `wrapMode=horizontal`: top/bottom edges do not wrap, left/right do
- [ ] `disableOnSingleMonitor=true`: unplugging one display disables wrapping
- [ ] Unplug/replug a display: the topology rebuilds after ~0.5 s and wrapping follows the new layout
- [ ] A layout with gaps (displays not flush in Display Settings): the gaps are listed in diagnostics and behaviour is explainable
- [ ] Remote desktop session: single-monitor semantics, no false reports
- [ ] Push the pointer over an edge continuously for 30 minutes: no memory or handle growth, no stutter
- [ ] Status row and notifications read correctly in both zh and en locales
- [ ] `copyDiagnostics` output contains no window titles, file names or user names

## L5 — Calibration and release

| Item | Content |
|---|---|
| Threshold calibration | Capture a real layout with `tools/capture_layout.ps1`, add it under `tests/layouts/` as a regression, and confirm the 50 px adjacency tolerance and 50 px guard radius suit that layout |
| Packaging | `python tools/package.py` produces `.wsplugin` + `.sha256` + `release-notes.md`; `--tag` validates the version |
| Release validation | `python tools/ci_release.py dist` locally, and CI's verify → build → publish |
| Documentation | Bilingual README, CHANGELOG `[0.2.0]`, `docs/spec*`, `docs/tests*` cross-links |
| Icon | `winshun-plugin-icons` generates `app.ico` and the `.rc`; registered in `plugins.json` |

## Traceability matrix

| Spec section | Tests covering it |
|---|---|
| §3 command and actions | L1 diagnostics shape, every L3 action check, L4 item 2 |
| §4 gesture and event swallowing | L1 mode/activation cases, L2 injected events (not automated), L4 items 3–8 |
| §5 topology algorithm | L1 topology group + fixtures + `nearestMonitor`, L5 threshold calibration |
| §6 anti-oscillation and direction | L1 engine and corner cases, L4 items 3–4 |
| §7 settings | L1 settings parsing / type confusion / both window settings round-tripping, L3 settingsChanged, L4 items 6–11 |
| §8 architecture and protocol | all of L3, L4 item 1, the data-window submenu (L3) |
| §9 data | L0 manifest checks, L1 per-reason counters / decision log / `lastSample`, L3 diagnostics and `data` submenu |
| §10 failure handling | L1 empty/degenerate/extreme coordinates, L2 fault injection (not automated), L4 items 12–15 |
| §11 permissions | L0 manifest checks, L4 item 1 |

## Acceptance criteria

- L1 green (currently 116 cases + 7 fixtures, including the captured
  `live-single.json`)
- L3 fully passing (currently 118/118)
- L0 static validation passing
- The single-display part of L4 has been measured and passed; **all multi-display
  boxes must be checked** before multi-display can be called releasable, which
  remains a release blocker

## Not tested (§未验证)

- L2 injected events and the `SetCursorPos` failure path have no automated case
  (they need a real hook or fault injection).
- L4's multi-display part is not executed item by item: multi-display feel,
  hot-plug, drag suppression, remote sessions, exclusive fullscreen, high
  contrast and Magnifier are unverified on real hardware.
- The watchdog's reinstall path has never fired on a real machine — only code
  review and the indirect observation in the data window (`hook live`).
- The data window was inspected once on real hardware; mixed-DPI layouts are
  unverified.
- `edgeThresholdPx` is wired into the decision (1 px default) but only verified
  against synthetic geometry.

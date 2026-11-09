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

## L1 — Pure logic unit tests (`cw_tests`, ctest target, 43 cases + 6 layout fixtures)

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
| Diagnostics shape | display count, outer-edge count, hook flag, gap text | §3 |
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

Run on 2026-11-10: **32 checks passed**, process exit code 0.

| Check | Spec |
|---|---|
| `initialize` has no error and reports `protocolVersion == "1"` | §8 |
| An empty query returns exactly one row, `id == cursorwrap.status`, with a title and subtitle | §3 |
| Three actions in Enter / Ctrl+Enter / Shift+Enter order | §3 |
| A query with text still returns one row (queries have no side effects) | §3 |
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

Needs a real desktop and at least two displays. Nothing in this section has been
run (this session has a single display).

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
| Documentation | Bilingual README, CHANGELOG `[0.1.0]`, `docs/spec*`, `docs/tests*` cross-links |
| Icon | `winshun-plugin-icons` generates `app.ico` and the `.rc`; registered in `plugins.json` |

## Traceability matrix

| Spec section | Tests covering it |
|---|---|
| §3 command and actions | L1 diagnostics shape, every L3 action check, L4 item 2 |
| §4 gesture and event swallowing | L1 mode/activation cases, L2 injected events (not automated), L4 items 3–8 |
| §5 topology algorithm | L1 topology group + fixtures, L5 threshold calibration |
| §6 anti-oscillation and direction | L1 engine and corner cases, L4 items 3–4 |
| §7 settings | L1 settings parsing/type confusion, L3 settingsChanged, L4 items 6–11 |
| §8 architecture and protocol | all of L3, L4 item 1 |
| §9 data | L0 manifest checks, L4 item 17 |
| §10 failure handling | L1 empty/degenerate/extreme coordinates, L2 fault injection (not automated), L4 items 12–15 |
| §11 permissions | L0 manifest checks, L4 item 1 |

## Acceptance criteria

- L1 green (currently 43 cases + 6 fixtures)
- L3 fully passing (currently 32/32)
- L0 static validation passing
- L4 fully checked before v0.1.0 can be called releasable; L4 has not been run, so it
  remains a release blocker

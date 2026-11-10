# Cursor wrap (`ra9fael.cursorwrap`) — design and test plan

Status: v0.1.0 implemented. Protocol and manifest fields follow WinShun 0.22.0
plugin docs (resident JSON-RPC over stdio). See [tests.md](tests.md) for evidence
and unverified items. Chinese original: [spec.md](spec.md).
Implementation and validation record: [validation.en.md](validation.en.md).

## 1 Goal

- Multi-monitor: wrap the pointer from a left/right/top/bottom outer edge to the
  opposite edge, so long drags across the desktop become short moves
- A pure logic core testable with no display attached (monitors injected as structs)
- A keyboard path: the `cursorwrap` keyword shows status, toggles, cycles the mode,
  copies diagnostics

## 2 Non-goals

- No tray icon, tray submenu or settings window (v0.1 has no windows at all)
- No global hotkey (the "activation" is holding Ctrl/Shift, not a registered chord)
- No cursor magnetism/snapping — outer-edge wrapping only
- No reimplementation of PowerToys' `wrap_simulator.py` tk visualiser

## 3 Command (`cursorwrap` / `wrap`)

An empty query returns one status row:

- Title: `Cursor wrap: on` / `Cursor wrap: off`
- Subtitle: `mode <mode> · activation <mode> · N display(s) · M outer edge(s)`;
  when the hook failed to install, ` · hook failed, status only` is appended
- Actions (order = Enter / Ctrl+Enter / Shift+Enter):
  - `toggleEnabled` — turn wrapping on/off
  - `cycleMode` — both → vertical only → horizontal only → both
  - `copyDiagnostics` — copy multi-line diagnostics (mode, activation, display
    count, outer-edge count, move/wrap/suppressed/rebuild counters, thresholds,
    layout gaps)
- Query text is ignored: a query never changes state

## 4 Gesture (the plugin's only automatic behaviour)

- Watches `WM_MOUSEMOVE` from a `WH_MOUSE_LL` hook and decides per settings
- On a wrap: `SetCursorPos` to the destination and swallow **that one** move
  (`return 1`). If `SetCursorPos` fails the event is passed through instead
- Movement only: buttons, wheel and `LLMHF_INJECTED` (programmatic) input are
  observed but never wrapped and never swallowed
- Decision order (same as PowerToys `HandleMouseMove`): single-monitor disable →
  drag suppression → activation key not held → anti-oscillation radius → on an
  outer edge? → destination

## 5 Topology algorithm

Ported from PowerToys `MonitorTopology` (MIT, Microsoft):

1. Four edges per display, keyed by (topology index, edge type). HMONITOR handles
   change; indices are stable within one snapshot.
2. Adjacency: opposite edge types, positions within the tolerance (default 50 px),
   and perpendicular ranges overlapping by more than the tolerance.
3. An edge adjacent to any other display is **inner** and never wraps; the rest are
   **outer** edges.
4. Destination: first an opposite outer edge whose range contains the cursor
   coordinate (overlap — the perpendicular coordinate is preserved); otherwise the
   nearest opposite outer edge by coordinate distance with the coordinate clamped
   into its range (non-overlap projection, 1 px inset).
5. No opposite edge at all (single monitor): wrap to the same display's opposite
   edge.

Display snapshots are sorted primary-first, then left/top, so indices are stable
and tests are reproducible.

## 6 Anti-oscillation and direction

- Guards radius (`wrapGuardPx`, default 50): after a wrap, samples within that
  radius of the landing point do not wrap; leaving the radius re-arms it. Without
  it a cursor landing on the opposite edge would immediately bounce back.
- At a corner two edges match; the dominant movement axis decides (horizontal
  movement prefers left/right edges, vertical prefers top/bottom). If the dominant
  candidate has no destination, the other one is tried.

## 7 Settings

| Key | Type | Default | Meaning |
|---|---|---|---|
| `wrapMode` | choice | both | both / vertical / horizontal |
| `activationMode` | choice | always | always / ctrl / shift (wrap only while held) |
| `disableDuringDrag` | bool | true | no wrapping while the left button is held |
| `disableOnSingleMonitor` | bool | false | disable when only one display is attached |
| `edgeThresholdPx` | number | 1 | 1–20, how close to an outer edge counts as on it |
| `wrapGuardPx` | number | 50 | 10–200, anti-oscillation radius |
| `adjacencyTolerancePx` | number | 50 | 10–200, inner-edge tolerance |
| `impactGate` | bool | false | impact mode: the cursor must hit the edge fast enough to wrap |
| `impactSpeedPps` | number | 2000 | 500–20000, the impact speed gate (px/s) |

Invalid values (wrong type, out of range, NaN/Inf) fall back to the manifest
default. A bad settings file must never error out or produce out-of-range
behaviour. `settingsChanged` merges: fields not present keep their current value.

### Impact mode (`impactGate`)

Off (default): reaching an outer edge wraps, exactly like PowerToys.

On: a wrap must also pass a speed gate — the **perpendicular speed toward the
edge over the recent 100 ms window** must reach `impactSpeedPps`. Design
choices:

- A window speed, not per-event acceleration: at 1000 Hz report rates the
  displacement between adjacent events is pure noise — the same conclusion
  PowerToys Find My Mouse reached for its `shakeMinSpeed` gate.
- Only the **perpendicular** axis counts: sliding along the edge (say, toward
  a taskbar icon) never triggers, however fast.
- **Unmeasurable speed fails closed**: too few samples in the window, missing
  or non-monotonic timestamps — no wrap. This blocks programmatic teleports
  (a big `SetCursorPos` jump) from masquerading as an impact.
- Timestamps come from `QueryPerformanceCounter` on the hook thread
  (microseconds), not `MSLLHOOKSTRUCT.time` (millisecond resolution, wraps).
- The movement history clears after every wrap: the speed of the hit cannot
  leak into the next decision.

## 8 Architecture

One process, one executable (`cursorwrap.exe`), no DLL, no Qt, no windows, no tray.
Three threads:

| Thread | Responsibility |
|---|---|
| Protocol (`serve()`) | `query` / `action` / `settingsChanged` / `shutdown`; owns a `WrapEngine` |
| Hook | `WH_MOUSE_LL` with its own message pump; settings arrive via `WM_APP_SETTINGS`, so the hot path is lock-free |
| Watcher | message-only window receiving `WM_DISPLAYCHANGE` / `WM_SETTINGCHANGE` / `WM_DEVICECHANGE`; rebuilds the topology after a 500 ms debounce |

The pure core (`wrap_topology`, `wrap_logic`) includes no OS headers — monitors
enter as `MonitorBox` values — so the engine unit-tests anywhere.

## 9 Data

`WS_DATA_DIR` is unused: no state file, no log file, no cache. Settings are managed
by the host in `WinShun.ini`. Diagnostics reach the clipboard only on
`copyDiagnostics` and contain counters and geometry only (no window titles, file
names or user text).

## 10 Failure matrix

| Failure | Symptom | Degradation |
|---|---|---|
| `SetWindowsHookEx` fails | status row says the hook is not installed; `winshun/log` warn | keyword query and toggle still work |
| `SetCursorPos` fails | the move is passed through untouched | that wrap does not happen; no stuck cursor |
| Display enumeration empty | engine idles, swallows nothing | status shows 0 displays |
| Layout has coordinate gaps (GPU driver) | gaps within the tolerance still count as adjacent; larger ones become outer edges | gaps are listed in diagnostics |
| Remote session (RDP) | one virtual display | single-monitor semantics |
| Display hot-plugged | rebuilt after the debounce, movement history cleared | no crash, no stale edges |
| Locked session / secure desktop | no hook events | nothing happens (correct) |
| Invalid setting value | falls back to the default | status row shows the effective value |
| Host too old | ignored | keyword path works |
| 3 consecutive query timeouts | host restarts the plugin | `initialized()` rebuilds hook and topology |

## 11 Permissions, privacy

- `permissions: ["ui"]` — the low-level mouse hook and a message-only window. `ui`
  is only partially enforced by the host; the README says so.
- No `elevated`, `processes`, `network`, `files:write` or clipboard permission.
- Coordinates and display geometry stay in the process: no network, no disk.

## 12 Known limitations

- Needs a low-level mouse hook. If Windows removes it after a
  `LowLevelHooksTimeout` stall the plugin does not re-install it (same as
  findmouse); see the unverified list in tests.md.
- Exclusive-fullscreen D3D applications may not see the outer edges.
- High contrast / Magnifier not specifically verified.
- Differences from PowerToys CursorWrap: no `activation_shortcut` hotkey trigger
  and no auto-activate process whitelist.

## 13 Roadmap

- v0.1 (this release): everything above
- v0.2 candidates: per-display enable, gap-snapping suggestions, hook re-install
  after a timeout

## 14 Delivery report (v0.1.0 backfill)

### Measured

- Unit tests `cw_tests`: 54 cases plus 6 layout fixture files, all green
  (`ctest` 1/1 passed).
- Protocol round trip `tests/protocol_roundtrip.py`: 32 checks passed
  (initialize → query → action ×4 → settingsChanged ×2 → cancel → shutdown), exit
  code 0.
- Build: MinGW-w64 GCC 13.2 / CMake 3.29 / Ninja Release; `cursorwrap.exe` links
  the C++ runtime statically and lands at the repo root next to `plugin.json`.

### Deviations from this design

- §5's "same-monitor wrap, 2 px inset" does not actually occur: on a single display
  both left/right (and top/bottom) edges are outer, so `findOppositeOverlapping`
  always finds its own opposite edge and the destination is that edge's `position`
  (`right-1` / `bottom-1`) rather than `right-2`. PowerToys' `GetWrapDestination`
  takes the same branch, so behaviour matches; the guard radius absorbs the bounce.
- §5's "wrap to this display's opposite edge" is in practice "wrap to the outermost
  opposite edge of the whole layout" with two or more displays: with two displays
  side by side, the right display's outer right edge wraps to the left display's
  left edge (the opposite outer edge with the smallest position). This matches
  PowerToys' `FindNearestOppositeEdge` semantics (dragging across the whole desktop).
- `adjacencyTolerancePx` takes effect through `Topology::initialize`'s tolerance
  parameter. `edgeThresholdPx` is now wired into the decision too (the
  `edgeThreshold` argument of `Topology::onOuterEdge`); the default 1 px matches
  PowerToys, and a larger value widens the edge band.

### Not measured (release blockers)

- Real feel and false-positive rate on two or three physical displays: this session
  has a single-display environment, so every multi-display assertion comes from
  injected geometry snapshots.
- Real pointer movement with `SetCursorPos`, drag suppression feel, and
  coexistence with a Ctrl-held gesture from another plugin.
- Hot-plug / `WM_DISPLAYCHANGE` rebuild timing (only synthetic paths exercised).
- Remote sessions, exclusive fullscreen, high contrast, Magnifier.
- Installing the `.wsplugin` into a real WinShun host and enabling it: the round
  trip above drives the executable's stdio directly, it is not host integration.

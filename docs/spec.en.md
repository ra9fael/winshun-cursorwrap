# Cursor wrap (`ra9fael.cursorwrap`) — design and test plan

Status: v0.1.0 implemented. Protocol and manifest fields follow WinShun 0.22.0
plugin docs (resident JSON-RPC over stdio). See [tests.md](tests.md) for evidence
and unverified items. Chinese original: [spec.md](spec.md).
Implementation and validation record: [validation.en.md](validation.en.md).
Line-by-line comparison against the PowerToys original: [comparison.md](comparison.md).

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

### Empty text: the settings list (multiple rows)

Empty text returns a **multi-row settings list**, one row per setting, where Enter
is that row's main action — so every setting has its own key, free of the host's
three-keys-per-row limit. **All twelve manifest settings are reachable here**; the
protocol test checks each one, so the two surfaces cannot drift apart silently:

| Row | Title | Enter | Manifest setting |
|---|---|---|---|
| 1 | `Cursor wrap: on/off` | turn wrapping on/off | — (runtime switch) |
| 2 | `Impact mode: on/off` | toggle impact mode | `impactGate` |
| 3 | `Impact speed: <n> px/s` | drill down to the speed submenu | `impactSpeedPps` |
| 4 | `Impact band: <n> px` | drill down to the band submenu | `impactBandPx` |
| 5 | `Wrap mode: <mode>` | cycle both → vertical → horizontal | `wrapMode` |
| 6 | `Activation: <mode>` | drill down to the activation submenu | `activationMode` |
| 7 | `Wrap guard: <n> px` | drill down to the radius submenu | `wrapGuardPx` |
| 8 | `Edge threshold: <n> px` | drill down to the threshold submenu | `edgeThresholdPx` |
| 9 | `Adjacency tolerance: <n> px` | drill down to the tolerance submenu | `adjacencyTolerancePx` |
| 10 | `No wrap while dragging: on/off` | drill down to the on/off submenu | `disableDuringDrag` |
| 11 | `Disable on one monitor: on/off` | drill down to the on/off submenu | `disableOnSingleMonitor` |
| 12 | `Tuning overlay: on/off` | drill down to the tuning submenu | `tuningOverlay` |
| 13 | `Data window: on/off` | drill down to the data-window submenu | `dataWindow` |
| 14 | `Suggested gate: <n>/not enough data` | drill down to the suggestion submenu | a `impactSpeedPps` suggestion from the trace |
| 15 | `Cursor wrap: arrival trace` | copy the trace CSV | — |
| 16 | `Cursor wrap: diagnostics` | copy the full diagnostics | — |
| 17 | `Restore host settings` | clear the search-bar overrides | — |

**The settings page and the search bar share one switch.** `tuningOverlay` and
`dataWindow` are real manifest settings: the settings page's toggle, the search
bar's `tune` / `data` submenus and the `settings.json` overrides file all write
the same field, and both survive a restart. The tuning switch used to exist only
in the search bar.

Status-row subtitle: `mode <mode> · activation <mode> · impact on/off [gate] ·
N display(s)`; when the search bar has overridden settings, ` · search-bar
overrides active (reset to fall back)` is appended; when the hook failed to
install, ` · hook failed, status only` is appended.

### Submenus (drill-down)

A drill-down row carries `setQuery`: Enter fills `cursorwrap <word> ` into the
search box, which re-queries and shows the submenu. **A submenu's first row is
always `← Back`** (`setQuery` back to `cursorwrap `); every other row is one
choice, and Enter applies it:

| Input | Submenu |
|---|---|
| `impact` | back · impact toggle · impact speed · impact band |
| `speed` | back · 500/1000/2000/3000/5000/8000/12000/20000 px/s |
| `band` | back · 50/80/120/150/200/300/500 px |
| `mode` | back · both · vertical only · horizontal only |
| `activation` | back · always · while Ctrl · while Shift |
| `guard` | back · 10/30/50/80/120/200 px |
| `edge` | back · 1/2/5/10/20 px |
| `tol` | back · 10/30/50/80/120/200 px |
| `drag` | back · on · off (no wrap while dragging) |
| `single` | back · on · off (disable on one monitor) |
| `tune` | back · switch · band · gate · guard · edge threshold (see "Tuning overlay") |
| `data` / `window` | back · switch · skip reasons · hook health · copy the trace · suggested gate (see "Data window") |
| `best` | back · suggested gate · apply · see the data (see "Arrival trace") |

### With a number: set directly (skip the submenu)

`cursorwrap <word> <number>` sets the value and shows the matching submenu with
the new value marked "current":

| Input | Effect |
|---|---|
| `impact on` / `impact off` | set the impact gate directly |
| `speed 3000` | the impact gate (500–20000, clamped; setting a speed turns the gate on) |
| `band 300` | the measuring band (50–500) |
| `mode vertical` etc. | set the direction directly |
| `ctrl` / `shift` / `always` | the activation mode |
| `guard 120` | the anti-oscillation radius (10–200) |
| `edge 5` | the edge threshold (1–20) |
| `tol 80` | the adjacency tolerance (10–200) |
| `drag on` / `drag off` | whether dragging wraps |
| `single on` / `single off` | whether one display disables wrapping |
| `tune on` / `tune off` | the tuning overlay (with no value it opens the submenu) |
| `data on` / `data off` | the data window (with no value it opens the submenu) |
| `trace` | a trace row; Enter copies the CSV |
| `best` | the suggestion submenu: see / apply / inspect |
| `clear` | drop the recorded arrivals |
| `reset` | clear the search-bar overrides; the host card applies again |
| `diag` | a diagnostics row; Enter copies the full text |
| anything else | a `valid:false` hint row listing the words |

**Persistence and precedence**: the host has no plugin→host settings channel (the
protocol only accepts `winshun/log|notify|progress|refresh|hotkeys|tool`; unknown
methods are dropped), and `WinShun.ini` is the host's file — the plugin does not
touch it. Search-bar changes go to the plugin's own data directory,
`WS_DATA_DIR/settings.json` (only keys that differ from the defaults), and are
applied on top of the host's settings at startup and in `settingsChanged`; `reset`
deletes the file. The host's settings card stays authoritative for **keys the search
bar has not overridden** — both places work, and the status row says which source is
active.

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
| `impactBandPx` | number | 150 | 50–500, the impact measuring band (how close to the edge counts as approach) |
| `tuningOverlay` | bool | false | tuning overlay: draw the measuring band and the wrap line on outer edges, plus the measured speed |
| `dataWindow` | bool | false | data window: live histogram, decision log, per-reason skip counts, hook health |

`tuningOverlay` and `dataWindow` are **real settings**, not command-layer state:
the settings page, the search bar (`tune` / `data`) and the `settings.json`
overrides file write the same field, and both survive a restart. Opening and
closing both windows goes through one place (`applyWindowSettings()`), called
after every path that can change them.

The wrap **landing is one pixel inside** the destination edge (`left+1` /
`right-2` / `top+1` / `bottom-2`), not on its line — a landing inside the
detection band would make the guard eat an immediate re-trigger.

### Tuning overlay

Search-bar `tune` opens the tuning submenu (switch plus drill-downs for band,
gate, guard and threshold); `tune on` / `tune off` flips it directly. When on:

**Only outer edges are drawn; the middle of a monitor is untouched.** That took
two rewrites to get right: the first version used colour-key transparency over
whole monitors, so when the key failed the desktop turned into an opaque sheet
(the teal panel in the reported screenshot) — and the middle never needed
anything drawn on it anyway. What is drawn now:

| Visualization | What is drawn |
|---|---|
| Outer edge line | a 2px accent line (alpha 235) on every edge that can actually wrap |
| Measuring band | a **translucent** strip (alpha 24), `impactBandPx` wide (min 8px), inside that line |
| Inner edges | **nothing** — two monitors touching never wrap, so drawing them is noise |
| Speed readout | on the last arrival, a small panel by that edge: `measured / gate px/s` with a state-coloured border (green wrapped, amber blocked, grey gate off) |
| Arrival trace | a histogram panel at the primary monitor's bottom-right (next section) |

Implementation notes:

- **Per-pixel alpha is required** (translucent band + crisp line), so it uses
  `UpdateLayeredWindow(ULW_ALPHA)` rather than `LWA_COLORKEY` (a colour key is
  all-or-nothing per pixel).
- **Each edge strip builds one DIB** — ~1.5 MB for a 150px band on a 4K panel.
  The earlier version allocated a full `2560×1440` DIB per frame (~33 MB) and
  blew the plugin's 64 MB job cap, which crashed it.
- Static strips are rebuilt only when the topology or the band width changes;
  the readout repaints only when the verdict or its numbers change.
- Window styles `WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE |
  WS_EX_TOOLWINDOW | WS_EX_TOPMOST` with `WM_NCHITTEST → HTTRANSPARENT` for
  click-through; panels are placed inside `rcWork` so the taskbar cannot cut them.
- Colours follow the host's `appearance` (`accent` / `text` / `background`).
  The toggle is not persisted: the overlay starts off after a restart.

### Arrival trace and the "optimal gate"

A live readout alone can never tell you the optimal value — it answers one
question at a time. The real question is a **distribution** one: where do the
speeds from *intentional pushes* separate from the speeds of *walking past the
edge*?

The engine therefore records an **arrival trace** (`Arrival`): one row per
arrival at an outer edge — `t_us / monitor / edge / measured_pps / gate_pps /
wrapped / gate_off` — capped at 512 rows (`kTraceMax`). Three decisions matter:

1. **Blocked arrivals are recorded too.** The first version only recorded
   accepted hits, so the slow cluster was always missing, the histogram had one
   hump, and no suggestion was possible. Recording happens *before* the gate
   decides.
2. **One row per arrival.** A hand resting at the edge makes Windows clamp the
   pointer, producing hundreds of identical samples; they are one arrival
   (deduplicated by `edge + monitor`).
3. **One row answers every threshold**: `measured_pps` alone decides "would gate
   X have wrapped this?", so 512 rows are enough to sweep 500…20000.

`best` returns the suggestion (`suggestGate`, a pure function):

- sort the measurable speeds, drop the outer 10% on each side as outliers, and
  put the gate in the middle of the **widest relative gap** between consecutive
  values;
- a relative gap under 25% means one cluster only (no gap) → return 0 and say
  "not enough data" in the UI; **never invent a number**;
- clamp the result to 500–20000.

`trace` copies the CSV (`seq,t_us,monitor,edge,measured_pps,gate_pps,wrapped,
gate_off`) so the suggestion can be checked offline; `clear` empties the trace.

Invalid values (wrong type, out of range, NaN/Inf) fall back to the manifest
default. A bad settings file must never error out or produce out-of-range
behaviour. `settingsChanged` merges: fields not present keep their current value.

### Impact mode (`impactGate`)

Off (default): reaching an outer edge wraps, exactly like PowerToys.

On: a wrap must also pass a speed gate — the **speed of the last continuous
approach stroke toward the edge, measured inside the band** must reach
`impactSpeedPps`. The stroke is traced backwards from the newest sample and cut
at the first of:

- a sample farther than `impactBandPx` from the edge — a distant sprint does
  not count toward the measurement;
- a reversal of the approach direction — after a bounce, only the last hit
  counts;
- a gap over the 100 ms window — after a pause the stroke restarts;
- missing or non-monotonic timestamps — unmeasurable speed fails closed.

Design choices:

- The **last in-band stroke**, not a whole-window average: a fast distant
  flight dilutes the braking near the edge, so "fly over, then crawl in" would
  pass as a hit. Cutting at the band measures only what happened close to it.
- A minimal-travel bar (20 px): edge jitter over 2 px reaches thousands of px/s
  instantaneously, but such a stroke is not a hit — a net displacement below
  20 px fails outright. Same conclusion PowerToys Find My Mouse reached for its
  `shakeMinSpeed` gate: at 1000 Hz the displacement between adjacent events is
  pure noise.
- Only the **perpendicular** axis counts: sliding along the edge (say, toward
  a taskbar icon) never triggers, however fast.
- **Unmeasurable speed fails closed**: this blocks programmatic teleports
  (a big `SetCursorPos` jump) from masquerading as an impact.
- Timestamps come from `QueryPerformanceCounter` on the hook thread
  (microseconds), not `MSLLHOOKSTRUCT.time` (millisecond resolution, wraps).
- The movement history clears after every wrap: the speed of the hit cannot
  leak into the next decision.

## 8 Data window

The tuning overlay draws on the edges themselves and only has content at the
moment of a hit. The **data window** is what can sit beside the app while you
tune:

- A real top-level GDI window (`WS_POPUP | WS_CAPTION | WS_SYSMENU`) with
  `WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE`: always on top, absent
  from the taskbar and Alt+Tab, and it **never takes focus** (taking it would
  interrupt whatever you are tuning against).
- Refreshes itself every 150 ms (`SetTimer` + `InvalidateRect`); `WM_ERASEBKGND`
  returns 1 and the window paints every pixel, so it does not flicker.
- Top to bottom:
  1. state header: enabled / hook state / display count / outer-edge count;
  2. the pointer position and the **most recent decision** (verdict + edge +
     measured speed + the gate in force) plus the `moves / wraps / skipped`
     totals;
  3. the arrival-speed histogram: log axis 500–20000 in 22 buckets, an amber
     line for the current gate and a green one for the suggested gate, with
     `N hits` and the suggestion in the title;
  4. the per-reason skip counters;
  5. the last 20 decisions (edge · measured speed · verdict · display), wraps in
     green and skips in grey.
- Closing it with the X clears the `dataWindow` setting too (the plugin thread
  polls `takeCloseRequest()` every 100 ms), so the two surfaces cannot disagree.

## 9 Per-reason skip counters and the decision log

A single `suppressed` total cannot locate "it never wraps": any of nine gates may
be saying no. `SkipReason` splits it:

| Reason | Meaning |
|---|---|
| `no-monitors` | no display was enumerated |
| `single-monitor` | one display, and "disable on one monitor" is on |
| `dragging` | the left button is held and "no wrap while dragging" is on |
| `no-modifier` | activation requires Ctrl/Shift and it is not held |
| `guard` | inside the anti-oscillation radius |
| `off-monitor` | the pointer is on no display (should not happen) |
| `not-on-edge` | not within the outer-edge detection band |
| `impact-gate` | reached the edge, but the measured speed failed the gate |
| `wrapped` (`None`) | it wrapped |

`Stats::skipped[]` counts **every sample**. The `Decision` log records only an
arrival or a CHANGE of skip reason — one continuous glide is one entry, so a
1000 Hz mouse cannot flood it — capped at 24 (`kDecisionMax`). Diagnostics, the
`data` submenu and the data window all read the same data.

## 10 Architecture

One process, one executable (`cursorwrap.exe`), no DLL, no Qt, no tray. Threads:

| Thread | Responsibility |
|---|---|
| Protocol (`serve()`) | `query` / `action` / `settingsChanged` / `shutdown`; owns a `WrapEngine` |
| Hook | `WH_MOUSE_LL` with its own message pump; settings arrive via `WM_APP_SETTINGS`, so the hot path is lock-free |
| Watcher | message-only window receiving `WM_DISPLAYCHANGE` / `WM_SETTINGCHANGE` / `WM_DEVICECHANGE`; rebuilds the topology after a 500 ms debounce |
| Coalescer | 100 ms poll: folds the hook's `refreshPending` into one `refresh()`, and turns the data window's close request into a setting change |
| Watchdog | 1 s comparison of `GetCursorPos()` against the position the hook last reported, to detect a silently dropped hook |
| Window threads | one `GetMessage` loop each for the tuning overlay and the data window |

### Zero I/O on the hook thread (a hard constraint)

A low-level mouse hook is timed by a watchdog inside Windows: a callback past
`LowLevelHooksTimeout` (300 ms by default) is **silently unhooked**, while the
plugin still believes `installed == true`. So the hook callback does **no I/O at
all**, not even a log line:

- a wrap only sets the atomic `m_refreshPending`; the coalescer turns it into one
  `refresh()`;
- the watchdog only compares atomics, and requests a repair with
  `PostThreadMessage(WM_APP_REINSTALL)`.

**The trap that matters**: after a successful `SetCursorPos` the landing point
must immediately be recorded as "the position the hook last saw", or the next
watchdog cycle sees "the pointer moved but no event arrived" and mistakes the
plugin's own wrap for a dead hook.

### Pointer location uses the nearest display, not exact containment

`Topology::monitorAt` is an exact containment test (the layout tests and gap
detection rely on it; it returns -1 outside). The wrap path uses
`Topology::nearestMonitor`: the container if there is one, else the closest
display. On a real machine the pointer can briefly sit outside every monitor
rect (topology not rebuilt yet, a hot-plug seam, gaps between the displays
Windows reports); exact containment then marks the whole path `off-monitor` and
swallows the wrap — a real-machine run logged 1008 such samples. The pure core
(`wrap_topology`, `wrap_logic`) includes no OS headers — monitors enter as
`MonitorBox` values — so the engine unit-tests anywhere.

### Remote Desktop (RDP)

An RDP session affects this plugin in three ways, none of them "unsupported":

1. **One virtual display.** That is a working configuration: on a single display
   wrapping goes to that display's opposite edge (right edge → left edge `x=1`,
   left edge → right edge `x=1918`). Measured on a real RDP session: 6/6 wraps.
2. **`disableOnSingleMonitor` silently switches wrapping off.** It is the only
   setting that makes a one-display user conclude the plugin is broken, so the
   status row spells it out — `disabled on one monitor: only one display, so
   nothing wraps` — and marks the session as `remote session`.
3. **A reconnect or resize fires `WM_DISPLAYCHANGE` while the enumeration can be
   empty.** That was a real bug: the old code applied the empty list as the new
   layout, erased the topology and left the plugin inert for the rest of the
   session. Now `rebuild` refuses an empty enumeration (keeping the last good
   layout and counting it in `rebuildRefused`), and `enumerateMonitors` falls
   back to the `SM_*VIRTUALSCREEN` rectangle when it comes back empty. Genuine
   changes (unplugging a monitor, the session ending and the desk layout
   returning) still apply immediately.

Injected input does not break RDP: `SendInput` events carry `LLMHF_INJECTED`, and the
filter that drops them looks like it would block a Remote Desktop user's real input.
Measurement says it does not — `injected_seen=16` with `delta_wraps=1`, because the
same movement also produces an untagged event for the engine to judge.

### The microsecond clock must not overflow (`qpc_time.h`)

The hook converts `QueryPerformanceCounter` to microseconds for the engine. Spelling
that `counter * 1'000'000 / freq` is wrong: Windows reports `freq` = 10 MHz, so
`counter * 1e6` **overflows int64** after about **10.7 days** of uptime
(`2^63 / 1e6 / 1e7` s), and every timestamp after that is negative. The consequence is
not "a slightly wrong speed" but **the gate refusing everything**: `approachSpeedPps`
divides by a negative window, yields `measured_pps = 0`, and every arrival is rejected
by `impact-gate` — the symptom is "impact mode is on, so nothing ever wraps".
Measured at 11.2 days up: `t_us = -875612248758`, `measured_pps = 0`.

`counterToUs(now, freq)` splits the division: `whole = now / freq`,
`rem = now % freq`, result `whole * 1e6 + rem * 1e6 / freq`, so neither product comes
close to the int64 ceiling. `freq <= 0` or a negative counter returns 0 (fail closed).
**Every** microsecond reading must go through it; no bare expression is allowed.

## 11 Data

Settings are managed by the host in `WinShun.ini`; search-bar changes go to
`WS_DATA_DIR/settings.json` (only keys that differ from the defaults). Otherwise
there is no log file and no cache. Diagnostics reach the clipboard only on
`copyDiagnostics` and contain counters and geometry only (no window titles, file
names or user text).

## 12 Failure matrix

| Failure | Symptom | Degradation |
|---|---|---|
| `SetWindowsHookEx` fails | status row says the hook is not installed; `winshun/log` warn | keyword query and toggle still work |
| The hook is silently unhooked | the watchdog reinstalls it after 2 cycles and counts it, `log(warn)` | recovers without a restart |
| `SetCursorPos` fails | the move is passed through untouched and counted in `setcursorpos failures` | that wrap does not happen; no stuck cursor |
| Display enumeration empty | engine idles, swallows nothing | status shows 0 displays |
| 指针短暂落在所有屏幕矩形之外 | 归到最近的一块屏继续判定 | 不再当作 `off-monitor` 吞掉 |
| An RDP reconnect or resize returns an empty enumeration | `rebuild` refuses it and counts it; `enumerateMonitors` falls back to the virtual-screen rect | the last good layout survives; the session is not left inert |
| Layout has coordinate gaps (GPU driver) | gaps within the tolerance still count as adjacent; larger ones become outer edges | gaps are listed in diagnostics |
| Remote session (RDP) | one virtual display | single-monitor semantics |
| Display hot-plugged | rebuilt after the debounce, movement history cleared | no crash, no stale edges |
| Locked session / secure desktop | no hook events | nothing happens (correct) |
| Invalid setting value | falls back to the default | status row shows the effective value |
| Host too old | ignored | keyword path works |
| 3 consecutive query timeouts | host restarts the plugin | `initialized()` rebuilds hook and topology |

## 13 Permissions, privacy

- `permissions: ["ui"]` — the low-level mouse hook and a message-only window. `ui`
  is only partially enforced by the host; the README says so.
- No `elevated`, `processes`, `network`, `files:write` or clipboard permission.
- Coordinates and display geometry stay in the process: no network, no disk.

## 14 Known limitations

- Needs a low-level mouse hook. The watchdog (a 1 s position comparison) finds
  and reinstalls one Windows removed, but in the extreme case (removed again
  immediately after every reinstall) it shows as "the event count stops growing"
  — that is what `hook reinstalls` and `hook responsive` in the diagnostics are
  for.
- Exclusive-fullscreen D3D applications may not see the outer edges.
- High contrast / Magnifier not specifically verified.
- Remote Desktop works and is supported (one virtual display, single-display
  semantics; see §10). A `WM_DISPLAYCHANGE` during reconnect can no longer erase
  the topology, and the status row marks the session as `remote session`.
- Differences from PowerToys CursorWrap: no `activation_shortcut` hotkey trigger
  and no auto-activate process whitelist.

## 15 Roadmap

- v0.1 (this release): everything above
- v0.2 candidates: per-display enable, gap-snapping suggestions, hook re-install
  after a timeout

## 14 Delivery report (v0.1.0 backfill)

### Measured

- Unit tests `cw_tests`: 102 cases plus 7 layout fixture files, all green
  (`ctest` 1/1 passed).
- Protocol round trip `tests/protocol_roundtrip.py`: 103 checks passed
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
- Exclusive fullscreen, high contrast, Magnifier.
- The Remote Desktop disconnect/reconnect timing (`rebuildRefused` is unit-tested
  only; **single-display RDP wrapping itself is measured** 6/6, see
  `docs/validation.en.md`).
- Installing the `.wsplugin` into a real WinShun host and enabling it: the round
  trip above drives the executable's stdio directly, it is not host integration.

### v0.2.0: what changed and what is still open

The user reported, after enabling the plugin on a real dual-monitor machine, that
wrapping did not happen at all, with or without impact mode, and that neither the
tuning switch nor the data read-out was reachable from the settings page. Two
root causes were found by audit, both now fixed with regressions:

1. The hook's wrap callback called `refresh()`, writing to the host's stdout
   inside a `WH_MOUSE_LL` callback. Windows unhooks a low-level hook whose
   callback stalls past `LowLevelHooksTimeout`, silently. The callback now only
   sets an atomic flag (coalesced into one `refresh()` every 100 ms).
2. The wrap path located the display with exact containment, so any sample
   outside every monitor rect became `off-monitor` and was swallowed. A
   real-machine capture logged 1008 such samples. `Topology::nearestMonitor` now
   attributes such a pointer to the closest display.

Still open, and honestly not measurable from here:

- **The two root causes were found by reading code and by one real-machine
  capture, not by a passing real-machine wrap.** The regression tests reproduce
  the user's exact geometry and settings (`user_config_*` in
  `tests/test_impact_gate.cpp`), and they pass, but they drive the engine
  directly. Only the user can confirm the real gesture now wraps.
- The watchdog's reinstall path has never fired on a real machine: the unit
  tests cover `nearestMonitor` and the decision accounting, and the watchdog was
  exercised only by reading its atomics in the data window (`hook live`).
- The data window was rendered and inspected once (screenshot: header, pointer,
  histogram, skip counters, decision log all populated from a live hook). Its
  behaviour on a mixed-DPI layout has not been checked.

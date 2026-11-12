# Changelog

[简体中文](CHANGELOG.md)

All notable changes to this plugin. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and versions follow
[Semantic Versioning](https://semver.org/). The version in `plugin.json` is the
single source of truth; release tags are `v<version>`.

## [Unreleased]

## [0.3.0] - 2026-11-13

### Added

- **`cw_devshell`: the GUI and the decision logic, testable without the host and
  without a real mouse.** Every GUI check was blocked by two things: the
  real-machine probes need a real mouse, and a Remote Desktop session refuses
  `SetCursorPos` (it returns `FALSE`); and any movement of the user's own hand
  voids the round. This shell **links the same source files the plugin ships**
  (`wrap_logic.cpp`, `wrap_topology.cpp`, `overlay.cpp`, `data_window.cpp`,
  `settings_store.cpp`) and replaces only the input with a scripted sample
  stream, so it **never moves the real pointer**. Three modes:
  - `cw_devshell diag`: the real layout, outer edges, gaps and suggested gate;
  - `cw_devshell gui`: real windows (the real `Overlay` and `DataWindow`), keys
    `1` slow drift / `2` fast flick / `3` braking flick, `q` to quit;
  - `cw_devshell run <scenario>`: replay a scenario file and assert on it, with
    the exit code as the verdict.
- **`tests/scenarios/` plus `tests/devshell_scenarios.py`**: four scenarios
  covering both user complaints (braking into the edge does not wrap, a fast
  flick does, no reading exceeds the ceiling), the dual-display outer
  edges/seam/vertical mode, and the windows following their settings. All pass,
  and none of it needs a real mouse.
- `tools/devshell/capture.ps1` screenshots the GUI for a visual check of the
  overlay and the panels. `validate.py` gained static checks: every scenario must
  declare a layout and assert something, and CMake must build the same sources
  into the shell — otherwise "it passed in the dev shell" would stop meaning
  anything.

### Fixed

- **A scenario changing a setting did not drive its window**: `cw_devshell`
  initially pushed settings to the engine only, so the overlay kept drawing after
  `set tuningOverlay off` and any visual check would have concluded the wrong
  thing. Settings now sync the windows (the same contract as the plugin's
  `applyWindowSettings()`), which is what lets a pixel comparison prove the
  overlay paints only when it is on.

## [0.2.2] - 2026-11-13

### Fixed

- **The impact speed was the average over the whole flight, not the speed AT the
  edge**: the user reported that a fast travel from far away that has already
  slowed down at the edge still wrapped. That was real: the average over the
  whole approach band (`impactBandPx`, default 150) was inflated by the fast
  flight. Measured: a hand braking from 4000 px/s to 250 px/s into the edge read
  312–2020 px/s, over the 2000 px/s gate, and wrapped; the wider the band the
  worse it got (43 of 80 combinations at band 250). It now measures the
  **shortest window walked back from the arrival that still carries enough
  distance to represent a speed** (`kImpactMinTravelPx`) — the speed at the
  moment of impact. Across the same 400 braking cases the false positives fell
  from 109 to 30 and no longer scale with the band; the 4000→250 case now reads
  312 px/s and is refused.
  Cost: an extreme stop (10000→1500, almost invisible to the eye) can still
  exceed the gate — that is the granularity the shortest window has.
- **Readings in the tens of thousands: events were stamped when DELIVERED, not
  when they happened**: Windows queues mouse events and hands a batch to the
  hook, so positions differing by real distance carry timestamps microseconds
  apart. Measured: a real 2500 px/s hand whose 16 ms of travel is delivered in
  200 µs reads 40000 px/s. `Sample` now carries `osMs`
  (`MSLLHOOKSTRUCT.time`, the tick the driver produced the event) and
  `elapsedUs()` takes the **larger** of the two spans — a queue can only make a
  reading slower (failing closed is the safe direction), never faster. Same
  scenario measured: 20000 px/s without the driver timestamp, 2500 px/s with it
  (the true value). Readings are additionally clamped to a 20000 px/s ceiling, so
  a six-digit number can no longer appear at all.
- Fixed a coupled defect found along the way: the arrival step is often shortened
  by Windows clamping the pointer at the edge (measured 15 px against a normal
  16–17 px step), which dragged a genuine hit below the gate and **refused fast
  impacts**. The clamped step is now detected as "the arrival sits on the edge
  line and its step is shorter than the previous one" and measured from the last
  free sample instead.
- The approach band **stays a hard** limit: an outside sample takes no part in the
  measurement (or the flight goes back into the number). When the in-band travel is
  under 20 px the stroke is unmeasurable and fails closed; measured live, band 100
  → 0 px/s, 250 → 3587 px/s, 500 → 7266 px/s, which is the guidance to widen the
  band under coarse sampling — that is what a user-facing band setting is for.

### Verified on real hardware

Real machine (RDP single display, gate 2000 px/s, band 250): fast flick 6×15 ms →
`10000 px/s` wraps; 8×15 ms → `6875 px/s` wraps; slow drift 8×60 ms → `1815 px/s`
refused; very slow 8×120 ms → refused. All as intended.

## [0.2.1] - 2026-11-13

### Added

- **Remote Desktop (RDP) compatibility**: the status row and diagnostics mark
  `remote session`, so "this machine usually has two displays but now reports
  one" reads as the session type rather than a fault. Diagnostics gained
  `remote session` and `rebuilds refused (empty enumeration)`.
- With one display and `disableOnSingleMonitor` on, the status row now says
  `disabled on one monitor: only one display, so nothing wraps` — that is the
  one setting that can silently switch wrapping off over RDP, and leaving it
  unsaid makes the plugin look broken.
- New probes `tests/rdp_wrap_probe.py`, `tests/repeat_push_probe.py` and
  `tests/injected_wrap_probe.py`. They judge a wrap by the plugin's own counters
  (`wraps`, `moves`, the per-reason skips) and check the landing point against
  the documented one-pixel inset.

### Fixed

- **The impact-mode clock overflows after 10.7 days of uptime, which made "gate
  on" block every wrap**: the hook converted `QueryPerformanceCounter` to
  microseconds as `counter * 1'000'000 / freq`. Windows reports `freq` = 10 MHz,
  so `counter * 1e6` **overflows int64** once the machine has been up for about
  10.7 days (2^63 / 1e6 / 1e7 s), and every timestamp after that is negative.
  `approachSpeedPps` then divides by a negative window, yields
  `measured_pps = 0`, and the gate refuses every arrival — the symptom is
  "impact mode is on, so nothing ever wraps". Measured on the machine that
  exposed it (11.2 days up): `t_us = -875612248758`, `measured_pps = 0`, and
  every arrival refused with `impact-gate`. Fix: `counterToUs()` in the new
  `src/qpc_time.h` splits the division into `whole = now / freq` plus
  `rem = now % freq`, so neither product can overflow; the hook and the overlay
  both use it. After the fix, on the same machine: fast pushes at 4231–8002 px/s
  wrap, a slow 800 px/s drift is refused by `impact-gate`, and all 7 RDP checks
  pass. Four regression cases, including the real 11.2-day counter value and an
  end-to-end "still wraps with a long-running clock".
- **An RDP reconnect or resize could wipe the topology**: `EnumDisplayMonitors`
  can return an **empty list** at the instant a session reconnects or changes
  resolution, and `WM_DISPLAYCHANGE` fires inside exactly that window. The old
  code applied the empty list as the new layout, erasing a working topology and
  leaving the plugin inert for the rest of the session. Now:
  1. `WrapEngine::rebuild` refuses an empty enumeration (keeping the last good
     layout) and counts it in `rebuildRefused`;
  2. `enumerateMonitors` falls back to the virtual-screen rectangle
     (`SM_*VIRTUALSCREEN`) when the enumeration comes back empty, so no
     notification can leave the plugin without a layout.
  Real changes (unplugging a monitor, the session ending and the desk layout
  returning) still apply immediately.
- **The old single-display probe's oracle did not hold**: `tests/live_wrap_probe.py`
  decided "it wrapped" from `cursor_pos() != target`, which is equally true when
  the push merely stops short. In the captured evidence `after=[1888,540]`
  (commanded 1919) was recorded as a successful wrap, while a right-edge wrap on
  one display must land at `x=1`. The new probes check the plugin's `wraps`
  delta plus the landing coordinate; that earlier evidence is retracted in
  `docs/validation.md`.

### Verified on a real Remote Desktop session

`tests/repeat_push_probe.py` ran 3 rounds per direction on a real RDP session
(`remote=1`, one 1920×1080 virtual display): **6/6 wrapped**, landing at `x=1`
from the right edge and `x=1918` from the left, with `y_stable=True` (which
proves no outside force moved the pointer). `tests/injected_wrap_probe.py`
additionally shows injected input (`SendInput`, `LLMHF_INJECTED`) wraps too:
`injected_seen=16` with `delta_wraps=1` — so the filter that drops injected
events does **not** disable a Remote Desktop user's real input.

## [0.2.0] - 2026-11-12

### Added

- **Data window** (`cursorwrap data` / `window`, or the settings page's "Data
  window"): a real topmost GDI window that never takes focus
  (`WS_EX_NOACTIVATE`), repainting itself every 150 ms and stopping when closed.
  It shows the state header (enabled / hook / displays / outer edges), the
  pointer the engine last saw and its most recent decision, the arrival-speed
  histogram (log axis 500–20000 with both the current and the suggested gate
  drawn in), the per-reason skip counters, and the last 20 decisions (edge,
  measured speed, verdict). **This is the "not only offline analysis"** half:
  the histogram and the log update while you tune.
- **Per-reason skip counters** (`SkipReason`): `no-monitors`,
  `single-monitor`, `dragging`, `no-modifier`, `guard`, `off-monitor`,
  `not-on-edge`, `impact-gate`. A single `suppressed` total could never answer
  "which gate keeps refusing"; the diagnostics, the `data` submenu and the data
  window now all list the split.
- **Decision log** (capped at 24): one entry per arrival and one per CHANGE of
  skip reason (a continuous glide logs once, so a 1000 Hz mouse cannot flood
  it), carrying the position, display, edge, measured speed, the gate in force
  and the verdict.
- **Hook health watchdog**: an independent thread compares `GetCursorPos()`
  against the position the hook last reported once a second. Two consecutive
  cycles of "the pointer moved but no event arrived" mean Windows silently
  dropped the hook: it is reinstalled, counted and logged as a warning. The
  crucial trap: after our own `SetCursorPos` the landing point must be recorded
  as the last known position immediately, or our own wrap looks like a dead
  hook.
- Two new settings, `tuningOverlay` and `dataWindow`, so the plugin's settings
  page can turn on what the search bar can turn on — the tuning switch used to
  exist only in the search bar.
- Diagnostics gained: the per-reason skip split, hook events / injected events /
  reinstall count / responsiveness, `SetCursorPos` failures, and both window
  switches.

### Fixed

- **Wrapping did nothing at all on the real dual-monitor machine (cause 1)**:
  the hook's wrap callback called `refresh()` directly, i.e. it wrote JSON to
  the host's stdout and took the send lock. A low-level mouse hook is timed by
  a Windows watchdog: a callback past `LowLevelHooksTimeout` (300 ms by default)
  is **silently unhooked**, while the plugin still believes it is installed and
  healthy. The wrap callback now only sets an atomic flag; the protocol thread's
  100 ms coalescing timer turns it into one `refresh()`. Zero I/O on the hook
  path.
- **Wrapping did nothing at all on the real dual-monitor machine (cause 2)**:
  the wrap path located the display with an exact containment test
  (`Topology::monitorAt`). A pointer outside every monitor rect returned -1 and
  the whole move was swallowed as `off-monitor`; a real-machine run logged
  **1008** such samples. Added `Topology::nearestMonitor` (the container, else
  the nearest) and the wrap path uses it; `monitorAt` keeps its exact semantics,
  which the layout tests and gap detection rely on. New regressions: "a pointer
  one pixel outside the layout must not disable wrapping" and "an outside sample
  is attributed to the nearest display".
- The wrap callback now tests `HC_ACTION` explicitly instead of relying on the
  implicit `code >= 0` convention.
- The hook handle is held in a `std::atomic<long long>` and all reinstall and
  shutdown paths go through one place, so a repair cannot leak a handle or
  unhook the wrong one.

### Changed

- The tuning overlay and the data window moved from command-layer private state
  into `cw::Settings` (`tuningOverlay` / `dataWindow`) and run through the same
  clamping and persistence path as everything else: the settings page, the
  search bar and the `settings.json` overrides file agree, and both survive a
  restart.
- Closing the data window with its X clears its setting, so the two surfaces
  cannot disagree about whether it is open.
- Version bumped to `0.2.0` (new feature surface: minor).

### Added

- **Arrival trace and the optimal-gate suggestion** (`cursorwrap best`,
  `trace`, `clear`): one row per arrival at an outer edge (time, monitor, edge,
  measured speed, the gate in force, whether it wrapped, whether the gate was
  off), capped at 512 rows. **Blocked arrivals are recorded too** — recording
  only accepted hits leaves the slow cluster out, so the histogram has one hump
  and no suggestion is possible. One row per arrival: a hand resting at the edge
  makes Windows clamp the pointer and produces hundreds of identical samples.
  `suggestGate` drops the outer 10% on each side as outliers, then puts the gate
  in the widest relative gap between consecutive speeds; a gap under 25% means a
  single cluster, so it returns 0 and the UI says "not enough data" rather than
  inventing a number. `trace` copies the CSV (`seq,t_us,monitor,edge,
  measured_pps,gate_pps,wrapped,gate_off`) for offline checking.
- The tuner draws that trace as a **histogram** (log axis 500–20000 px/s): the
  accent line is the current gate, the green line the suggested one, and the
  header reads `N hits · M wrap`. It sits at the primary monitor's bottom-right
  inside `rcWork`, so the taskbar cannot cut it.
- The search bar now reaches **all ten manifest settings**: new `drag` and
  `single` submenus plus `drag on|off` / `single on|off` direct sets, and `tune`
  became a submenu (switch plus band / gate / guard / threshold drill-downs) with
  `tune on|off` setting it directly. The protocol test checks every manifest key
  against a row id so the two surfaces cannot drift apart.
- **Tuning overlay** (`cursorwrap tune`): **outer edges only — the middle of a
  monitor is untouched.** Each edge that can wrap gets a 2px accent line with a
  **translucent** `impactBandPx`-wide strip (min 8px) inside it; inner edges draw
  nothing. On arrival, a small panel by that edge shows `measured / gate px/s`
  (green wrapped, amber blocked, grey gate off). It uses per-pixel alpha
  (`UpdateLayeredWindow(ULW_ALPHA)`) with one DIB per strip (~1.5 MB for a 150px
  band on 4K), rebuilt only when the topology or band width changes. Colours
  follow the host theme's `accent`/`text`/`background`; the toggle does not
  persist.

### Fixed

- **Impact mode stopped working on real hardware**: once the hand reached an
  edge, Windows clamped the pointer and the repeated coordinates were averaged
  into the measured speed, dragging a genuine 2000 px/s hit down to 1787 … 0, so
  nothing ever passed the gate. The clamped tail is now stripped and the last
  step is measured to the last **free** sample instead — hitting the wall is not
  deceleration. Four regression tests, including "twelve repeats cannot drag
  2100 px/s below 2000".
- **The tuning overlay turned into an opaque screen-sized sheet**: colour-key
  transparency is all-or-nothing per pixel, so when the key missed the whole
  display took the background colour (the teal panel in the report). Replaced
  with per-pixel alpha and **outer edges only**; no window covers the middle any
  more.
- The tuning panel was cut in half by the taskbar; it is now placed inside
  `rcArea`/`rcWork`.
- **The tuning panel froze after its first paint**: rebuilding the edge strips
  destroyed the window that owned the repaint timer, so the timer went with it.
  The timer is now re-armed on the new first strip, and a changed gate value also
  triggers a histogram redraw (otherwise the gate line never moved). Verified on
  hardware: 12 slow arrivals drew one hump, then 12 fast ones added the second
  hump, the `best 2800` line appeared, and the header went from `12 hits` to
  `24 hits · 12 wrap`.

### Changed

- **The tuning overlay was rewritten**: from "one whole-monitor layered window
  per display with colour-key transparency" to "one per-pixel-alpha strip per
  outer edge, plus a readout panel and a trace histogram". A colour key cannot
  express a translucent band, and when it fails the screen is covered.
- **The wrap landing is now one pixel inside** the destination edge (`left+1` /
  `right-2` / `top+1` / `bottom-2`) instead of on its line. A landing inside the
  detection band made the guard eat an immediate re-trigger; the inset lands
  cleanly inside the monitor and back-to-back wraps feel smoother.
- The multi-row settings list grew from 11 to 16 rows, covering all ten manifest
  settings plus tuning / suggestion / trace / diagnostics / reset.
- `tune` opens a submenu instead of toggling immediately (`tune on|off` still
  sets it directly), so the overlay cannot be turned on by accident.

### Changed (previous)

- **The search bar now returns a multi-row settings list**: `cursorwrap` with no
  words no longer returns a single status row but an 11-row list — one row per
  setting, where Enter is that row's main action (toggle / cycle / drill down /
  copy / reset). Every setting gets its own key, free of the host's
  three-keys-per-row limit, and there are no Ctrl+Enter / Shift+Enter chords to
  remember.
- **Submenu drill-down**: `cursorwrap impact` / `speed` / `band` / `mode` /
  `activation` / `guard` / `edge` / `tol` open the matching submenu, whose **first
  row is always `← Back`**; every other row is one choice and Enter applies it.
  Drill-down uses the row's `setQuery` field (Enter fills the search box), so the
  vendored `winshun.h` `Item` gained `setQuery` / `withSetQuery()` (the host's
  `Items.cpp` already parsed that field; the SDK header did not expose it).
- New keywords `activation` (activation submenu) and `band` (measuring-band
  submenu).
- A number still sets the value directly and shows the matching submenu with the
  new value marked "current": `speed 3000`, `band 300`, `guard 120`, and so on.
- The unknown-word hint row lists the full vocabulary (including band and
  activation).

### Added

- Change settings from the search bar: `cursorwrap impact` (toggle the impact
  gate), `cursorwrap speed 3000` (the gate), `cursorwrap mode vertical` (the
  direction), `cursorwrap ctrl/shift/always` (activation), `cursorwrap
  guard/edge/tol <n>` (thresholds), `cursorwrap reset` (fall back to the host's
  settings card). Changes apply immediately and persist in the plugin's data
  directory (`WS_DATA_DIR/settings.json`, only keys that differ from the
  defaults); the host card stays authoritative for keys not overridden, and the
  status row says which source is active. zh/en keywords, out-of-range numbers
  clamp, unknown words get a hint row.
- A new `toggleImpact` action (Shift+Enter toggles impact mode). The host binds
  at most three keys per row (Enter/Ctrl+Enter/Shift+Enter — a fourth action has
  no key), so copying diagnostics moved to the search bar: `cursorwrap diag`
  returns a diagnostics row and Enter copies the full text.
- Impact mode (`impactGate` / `impactSpeedPps`): when on, the cursor must hit the
  edge fast enough to wrap, so drifting to the edge (say, toward the taskbar) no
  longer triggers. The speed is measured over the last continuous approach stroke
  toward the edge (500–20000 px/s, default 2000); sliding along the edge does not
  count, and an unmeasurable speed (missing or non-monotonic timestamps,
  programmatic teleports) fails closed. Verified on hardware: a fast push at
  ≈5300 px/s wraps, a slow drift at ≈800 px/s and a single teleport do not.
- Impact measuring band (`impactBandPx`, 50–500, default 150) and de-jitter: the
  speed is measured only within the band — a distant sprint is cut off and cannot
  dilute the braking near the edge; after a reversal only the last hit counts;
  edge jitter with under 20 px of travel fails outright; a pause over the window
  restarts the stroke. The movement history deepened to 128 entries to cover the
  full window at 1000 Hz report rates.
- Engineering fix: `WrapEngine` now takes an internal mutex (the protocol thread
  and the hook thread previously shared it without locking); the hook callback
  uses `QueryPerformanceCounter` timestamps (used by the impact speed window).

## [0.1.0] - 2026-11-10

### Added

- First release: multi-monitor cursor wrapping. The cursor enters from the
  opposite edge when it reaches an outer edge, driven by display-edge topology
  (the `wrap_topology` pure core, ported from PowerToys CursorWrap's algorithm:
  edge map, outer-edge detection, projection for non-overlapping regions), with
  both / vertical-only / horizontal-only wrap modes, always / Ctrl / Shift
  activation, drag suppression and single-monitor disable.
- The `cursorwrap` (`wrap`) keyword: empty query shows a status row; Enter
  toggles the plugin, Ctrl+Enter cycles the wrap mode, Shift+Enter copies
  diagnostics.
- Topology rebuild with a 500 ms debounce after `WM_DISPLAYCHANGE` /
  `WM_SETTINGCHANGE`.
- Layout fixture regression tests (`tests/layouts/*.json`) and 3×3 grid
  scenario unit tests.

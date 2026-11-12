# Changelog

[简体中文](CHANGELOG.md)

All notable changes to this plugin. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and versions follow
[Semantic Versioning](https://semver.org/). The version in `plugin.json` is the
single source of truth; release tags are `v<version>`.

## [Unreleased]

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

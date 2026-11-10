# Changelog

[简体中文](CHANGELOG.md)

All notable changes to this plugin. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and versions follow
[Semantic Versioning](https://semver.org/). The version in `plugin.json` is the
single source of truth; release tags are `v<version>`.

## [Unreleased]

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
  longer triggers. The speed is the displacement perpendicular to the edge over a
  100 ms window (500–20000 px/s, default 2000); sliding along the edge does not
  count, and an unmeasurable speed (missing or non-monotonic timestamps,
  programmatic teleports) fails closed. Verified on hardware: a fast push at
  ≈5300 px/s wraps, a slow drift at ≈800 px/s and a single teleport do not.
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

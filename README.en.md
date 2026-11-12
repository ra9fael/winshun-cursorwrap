# ra9fael.cursorwrap — multi-monitor cursor wrapping (WinShun plugin)

> English · [简体中文](README.md)

When the pointer is pushed off an **outer** edge of the screen layout it re-enters
from the **opposite** edge. With several displays side by side or stacked, long
drags across the desktop become short moves. The algorithm is ported from PowerToys
CursorWrap (MIT).

## Why

On a multi-display desktop, moving the pointer from the far right to the far left
means a long drag. Tools solve this with a hotkey or a hot corner, which you have to
remember. This plugin uses something you already do: pushing the pointer against the
edge of the screen. At an outer edge of the layout, it comes back in from the
opposite side.

Edges where two displays **touch** (inner edges) are left alone — Windows already
moves the pointer to the adjacent display there, so the plugin never interferes and
nothing flashes back.

## Usage

Type `cursorwrap` (or `wrap`) to get a **multi-row settings list** — one row per
setting, where Enter is that row's main action, so there are no chords to remember:

| Row | Enter |
|---|---|
| `Cursor wrap: on/off` | turn wrapping on / off |
| `Impact mode: on/off` | toggle impact mode |
| `Impact speed: <n> px/s` | drill down to the speed submenu |
| `Impact band: <n> px` | drill down to the band submenu |
| `Wrap mode: <mode>` | cycle both → vertical → horizontal |
| `Activation: <mode>` | drill down to the activation submenu |
| `Wrap guard: <n> px` | drill down to the radius submenu |
| `Edge threshold: <n> px` | drill down to the threshold submenu |
| `Adjacency tolerance: <n> px` | drill down to the tolerance submenu |
| `No wrap while dragging: on/off` | drill down to the on/off submenu |
| `Disable on one monitor: on/off` | drill down to the on/off submenu |
| `Tuning overlay: on/off` | drill down to the tuning submenu |
| `Suggested gate: <n>/not enough data` | drill down to the suggestion submenu |
| `Cursor wrap: arrival trace` | copy the trace CSV |
| `Cursor wrap: diagnostics` | copy the full diagnostics |
| `Restore host settings` | clear the search-bar overrides |

**Submenus** (after drilling down, the first row is always `← Back`; every other
row is one choice, and Enter applies it):

| Input | Submenu |
|---|---|
| `cursorwrap impact` | back · impact toggle · impact speed · impact band |
| `cursorwrap speed` | back · 500/1000/2000/3000/5000/8000/12000/20000 px/s |
| `cursorwrap band` | back · 50/80/120/150/200/300/500 px |
| `cursorwrap mode` | back · both · vertical only · horizontal only |
| `cursorwrap activation` | back · always · while Ctrl · while Shift |
| `cursorwrap guard` | back · 10/30/50/80/120/200 px |
| `cursorwrap edge` | back · 1/2/5/10/20 px |
| `cursorwrap tol` | back · 10/30/50/80/120/200 px |
| `cursorwrap drag` | back · on · off |
| `cursorwrap single` | back · on · off |
| `cursorwrap tune` | back · switch · band · gate · guard · threshold |
| `cursorwrap best` | back · suggested gate · apply · see the data |

**With a number, set directly** (skips the submenu):

| Input | Effect |
|---|---|
| `cursorwrap impact on` / `off` | set impact mode directly |
| `cursorwrap speed 3000` | the impact gate (500–20000 px/s; setting a speed turns it on) |
| `cursorwrap band 300` | the measuring band (50–500 px) |
| `cursorwrap mode vertical` | set the direction directly |
| `cursorwrap ctrl` / `shift` / `always` | the activation mode |
| `cursorwrap guard 120` | the anti-oscillation radius (10–200 px) |
| `cursorwrap edge 5` | the edge threshold (1–20 px) |
| `cursorwrap tol 80` | the adjacency tolerance (10–200 px) |
| `cursorwrap drag on` / `off` | whether dragging wraps |
| `cursorwrap single on` / `off` | whether one display disables wrapping |
| `cursorwrap tune on` / `off` | the tuning overlay |
| `cursorwrap trace` | a trace row; Enter copies the CSV |
| `cursorwrap best` | the suggestion: see / apply / inspect |
| `cursorwrap clear` | drop the recorded arrivals |
| `cursorwrap reset` | clear the search-bar overrides; the host card applies again |
| `cursorwrap diag` | a diagnostics row; Enter copies the full text |

Changes **apply immediately** and persist in the plugin's data directory
(`settings.json`, only keys that differ from the defaults); the host's settings card
stays authoritative for keys not overridden, and the status row says when overrides
are active.

## Tuning overlay

Impact parameters are hard to set by feel — `cursorwrap tune` opens the tuning
submenu (switch plus band / gate / guard / threshold), and `tune on` / `tune off`
flips it directly. When on, **only outer edges are drawn; the middle of a monitor
is untouched**:

- **Outer edge line**: a 2px accent line on every edge that can actually wrap —
  inner edges (two monitors touching) draw nothing, since nothing wraps there;
- **Measuring band**: a **translucent** `impactBandPx`-wide strip inside that
  line, so the band's size is visible at a glance;
- **Speed readout**: on arrival, a small panel by that edge shows
  `measured / gate px/s` with a state-coloured border — green wrapped, amber
  blocked, grey gate off. Set `speed` straight from what your flicks measure;
- **Arrival trace**: a histogram panel at the primary monitor's bottom-right
  (next section).

The overlay is click-through, never takes focus and has no Alt+Tab entry; colours
follow the host theme, and panels sit inside `rcWork` so the taskbar cannot cut
them. The toggle does not persist across restarts.

## Arrival trace: finding the optimal gate

A live readout alone can never tell you the optimal value — it answers one
question at a time. The real question is a distribution one: **where do the
speeds of intentional pushes separate from the speeds of walking past the edge?**

So the plugin records one row per arrival at an outer edge (up to 512 rows):

```
seq,t_us,monitor,edge,measured_pps,gate_pps,wrapped,gate_off
```

Two things matter:

1. **Blocked arrivals are recorded too** — recording only accepted hits leaves
   the slow cluster out, the histogram has one hump, and no suggestion is
   possible;
2. **One row per arrival** — a hand resting at the edge makes Windows clamp the
   pointer, producing hundreds of identical samples that all belong to one
   arrival.

The tuner's histogram draws exactly those rows on a log axis (500…20000 px/s):
the accent line is the **current gate**, the green line is the **suggested gate**.

`cursorwrap best` puts the gate in the **widest gap** between arrival speeds; when
there is no gap worth the name (one cluster only) it says "not enough data"
instead of inventing a number. "Use this gate" applies it immediately.

`cursorwrap trace` copies the CSV so the suggestion can be checked offline;
`cursorwrap clear` empties the trace.

**Two fixes that came with this**: the wrap landing moved from "on the destination
edge line" to **one pixel inside** (a landing inside the detection band made the
guard eat an immediate re-trigger), and the measured speed no longer averages in
the clamped samples at the edge — pushing against the edge used to dilute a real
2000 px/s hit down to 0, which is why wrapping appeared to stop working entirely.

## Settings

| Key | Default | Meaning |
|---|---|---|
| `wrapMode` | both | both / vertical only / horizontal only |
| `activationMode` | always | always wrap / while Ctrl is held / while Shift is held |
| `disableDuringDrag` | true | no wrapping while the left button is held |
| `disableOnSingleMonitor` | false | disable on a single display (default still wraps to its opposite edge) |
| `edgeThresholdPx` | 1 | 1–20, how close to an outer edge counts as on it |
| `wrapGuardPx` | 50 | 10–200, anti-oscillation radius around the landing point |
| `adjacencyTolerancePx` | 50 | 10–200, how close two edges must be to count as adjacent (inner) |
| `impactGate` | false | impact mode: the cursor must hit the edge fast enough to wrap; drifting there does not trigger |
| `impactSpeedPps` | 2000 | 500–20000, the impact speed gate (px/s, over the last in-band approach stroke) |
| `impactBandPx` | 150 | 50–500, the impact measuring band: speed is measured only this close to the edge; distant sprints and edge jitter both fail |

Invalid values fall back to the defaults; nothing errors out.

## Permissions

The plugin asks only for `ui` (windows and hooks), because it needs:

- a low-level mouse hook (`WH_MOUSE_LL`) to observe pointer movement;
- an **invisible** message-only window to receive display-change notifications.

It never asks for administrator rights, never touches the network, never reads or
writes files and never touches the clipboard. Pointer coordinates and display
geometry stay **inside the plugin process**: nothing is persisted, nothing is sent
anywhere.

The host enforces `ui` only **partially**; please accept or decline it with your own
judgement.

## Known limitations

- Exclusive-fullscreen DirectX applications may not see the outer edges
- If Windows removes the hook after a `LowLevelHooksTimeout` stall this version does
  not re-install it (the status row says "hook failed, status only"); re-enabling the
  plugin fixes it
- A remote desktop session has one virtual display, so single-display semantics apply
- High contrast and Magnifier have not been specifically verified
- Differences from PowerToys CursorWrap: no hotkey-triggered wrap mode and no
  per-process auto-enable

## Install

1. Download `ra9fael.cursorwrap-<version>.wsplugin` from
   [Releases](https://github.com/ra9fael/winshun-cursorwrap/releases/latest)
   (verify it with the `.sha256` sidecar: `Get-FileHash <file> -Algorithm SHA256`)
2. WinShun → **Settings → Plugins → Installed → Add plugin → Archive**, pick the file
3. Plugins installed from a local archive are **not enabled automatically**; flip the
   switch in the list. Enabling is where you confirm the `ui` permission

This is a native exe package and needs WinShun 0.22.0 or newer (the manifest's
`minWinShun` says so). WinShun recognises the same id, asks whether to replace the old
version and keeps the plugin's data directory; uninstall from
**Settings → Plugins → Installed**.

**Restart the plugin process after an upgrade** (disable and re-enable, or restart
WinShun) — a native exe does not hot-reload like a script.

**Manual install** (without the host UI): unpack the archive into
`%LOCALAPPDATA%\WinShun\plugins\ra9fael.cursorwrap\` (`plugin.json` must sit at the
root of that folder) and add to `WinShun.ini`:

```ini
[Plugin.ra9fael.cursorwrap]
Enabled=true
Confirmed=ui
```

Then restart WinShun. `Confirmed=ui` records that you accepted the `ui` permission;
without that line the host asks for confirmation when you enable the plugin.

## Build from source

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

Requires CMake 3.16+ and a C++17 compiler (MinGW-w64 or MSVC). `cursorwrap.exe` lands
at the repository root next to `plugin.json` (`runtime.program` is a relative path).
The GNU branch links the C++ runtime statically, so the target machine needs no
MinGW. MSVC builds use the static runtime (`/MT`) and likewise ship no external DLL.

## Tests

```powershell
ctest --test-dir build --output-on-failure       # L1: 102 cases + 7 layout fixtures
python tests/protocol_roundtrip.py               # L3: 103 protocol round-trip checks
python tests/live_wrap_probe.py                  # L4 single display: real SetCursorPos gestures
python tests/validate.py                         # L0: manifest / bilingual docs / packaging checks
```

Design notes: [docs/spec.en.md](docs/spec.en.md). Test plan: [docs/tests.en.md](docs/tests.en.md).
This round's validation evidence: [docs/validation.en.md](docs/validation.en.md).

## Packaging

```powershell
python tools/package.py            # dist/<id>-<version>.wsplugin + .sha256 + release-notes.md
python tools/package.py --tag v0.1.0   # also checks the tag matches plugin.json and the CHANGELOG section exists
```

The package contains every git-tracked file plus `cursorwrap.exe`, as a deterministic
zip (fixed timestamps, stored uncompressed): the same inputs always produce the same
SHA-256. The release flow (tag → CI verify/build/publish) lives in
[.github/workflows/release.yml](.github/workflows/release.yml) and [AGENTS.md](AGENTS.md).
The bilingual release notes are at
[docs/release-notes/v0.1.0.md](docs/release-notes/v0.1.0.md).

## License

MIT. The algorithm is ported from PowerToys CursorWrap (Microsoft Corporation, MIT);
see §5 of [docs/spec.en.md](docs/spec.en.md).

The plugin icon was generated from the Phosphor glyph `arrows-out-cardinal` by
[winshun-plugin-icons](https://github.com/ra9fael/winshun-plugin-icons).

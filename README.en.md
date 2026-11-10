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

Type `cursorwrap` (or `wrap`) to see the status row — **and put words after it to
change settings right there**:

| Input | Effect |
|---|---|
| `cursorwrap` (empty) | the status row |
| `cursorwrap impact` | toggle impact mode |
| `cursorwrap impact on` / `off` | set impact mode directly |
| `cursorwrap speed 3000` | the impact gate (500–20000 px/s; setting a speed turns it on) |
| `cursorwrap mode` | cycle the direction: both → vertical → horizontal |
| `cursorwrap mode vertical` | set the direction directly |
| `cursorwrap ctrl` / `shift` / `always` | the activation mode |
| `cursorwrap guard 120` | the anti-oscillation radius (10–200 px) |
| `cursorwrap edge 5` | the edge threshold (1–20 px) |
| `cursorwrap tol 80` | the adjacency tolerance (10–200 px) |
| `cursorwrap reset` | clear the search-bar overrides; the host card applies again |
| `cursorwrap diag` | a diagnostics row; Enter copies the full text |

Changes **apply immediately** and persist in the plugin's data directory
(`settings.json`, only keys that differ from the defaults); the host's settings card
stays authoritative for keys not overridden, and the status row says when overrides
are active.

| Key | Effect |
|---|---|
| Enter | turn wrapping on / off |
| Ctrl+Enter | cycle the mode: both → vertical only → horizontal only |
| Shift+Enter | impact mode on ↔ off |

The host binds at most three keys per row; diagnostics live at `cursorwrap diag`.

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
ctest --test-dir build --output-on-failure       # L1: 80 cases + 7 layout fixtures
python tests/protocol_roundtrip.py               # L3: 32 protocol round-trip checks
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

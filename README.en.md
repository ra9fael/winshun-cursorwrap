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

Type `cursorwrap` (or `wrap`) with no argument to see the status row:

| Key | Effect |
|---|---|
| Enter | turn wrapping on / off |
| Ctrl+Enter | cycle the mode: both → vertical only → horizontal only |
| Shift+Enter | copy diagnostics (mode, display count, outer-edge count, counters, thresholds, layout gaps) |

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
   (verify it with the `.sha256` sidecar if you like)
2. WinShun → **Settings → Plugins → Installed → Add plugin → Archive**, pick the file
3. Plugins installed from a local archive are **not enabled automatically**; flip the
   switch in the list. Enabling is where you confirm the `ui` permission

This is a native exe package and needs WinShun 0.22.0 or newer (the manifest's
`minWinShun` says so).

## Build from source

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

Requires CMake 3.16+ and a C++17 compiler (MinGW-w64 or MSVC). `cursorwrap.exe` lands
at the repository root next to `plugin.json` (`runtime.program` is a relative path).
The GNU branch links the C++ runtime statically, so the target machine needs no
MinGW.

## Tests

```powershell
ctest --test-dir build --output-on-failure       # L1: 43 cases + 6 layout fixtures
python tests/protocol_roundtrip.py               # L3: 32 protocol round-trip checks
```

Design notes: [docs/spec.en.md](docs/spec.en.md). Test plan: [docs/tests.en.md](docs/tests.en.md).

## Packaging

```powershell
python tools/package.py            # dist/<id>-<version>.wsplugin + .sha256 + release-notes.md
python tools/package.py --tag v0.1.0
```

## License

MIT. The algorithm is ported from PowerToys CursorWrap (Microsoft Corporation, MIT);
see §5 of [docs/spec.en.md](docs/spec.en.md).

The plugin icon was generated from the Phosphor glyph `arrows-out-cardinal` by
[winshun-plugin-icons](https://github.com/ra9fael/winshun-plugin-icons).

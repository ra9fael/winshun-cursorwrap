# Repository conventions

[简体中文](AGENTS.md)

Commit subjects and bodies use the English Angular format:
`type(scope): imperative English summary`; breaking changes get a
`BREAKING CHANGE:` body footer. One commit does one thing.

## Versioning and tags

`plugin.json`'s `version` is the single source of truth and the release tag is
`v<version>` (the value `tools/package.py --tag` validates). Semantic versioning:

| Situation | Bump | Example |
|---|---|---|
| A new user-visible feature surface, a new setting, command or window | **minor** | 0.1.0 → 0.2.0 |
| A bug fix, wording, internal refactor, tests only | **patch** | 0.2.0 → 0.2.1 |
| A compatibility break (removing a setting key, changing the manifest shape, raising `minWinShun`) | **major** | 1.0.0 |

Rules:

1. **Tag when a batch of changes lands** — do not hoard several releases worth.
2. Before tagging, fold `CHANGELOG.*`'s `[Unreleased]` into
   `## [<version>] - <YYYY-MM-DD>` and open a fresh empty `## [Unreleased]`;
   `tests/validate.py` cross-checks that heading against `plugin.json` and fails
   on a mismatch.
3. `python tools/package.py --tag v<version>` produces the package plus the
   `.sha256` and the release notes, then `git tag -a v<version> -m "..."`.
4. A published Release asset is **never replaced**: ship a new patch version.

Public Actions entry points are new version tag pushes and `workflow_dispatch`
only — no branch push, PR, or scheduled triggers. Tag jobs must check
`github.event.created && !github.event.deleted`.

Manual verification accepts a source ref with `publish` defaulting to false;
publishing requires an existing, matching version tag. Keep all tests plus
manifest, docs and package validation. Only the publish job gets
`contents: write` and must never overwrite existing Release assets.

## The low-level mouse hook red line

The `WH_MOUSE_LL` callback must contain **no I/O** — no log line, no
notification, no lock acquisition, no stdout write. Windows' own watchdog
unhooks a callback that stalls past `LowLevelHooksTimeout` (300 ms by default)
**silently**, and the plugin still believes it is healthy. Anything the outside
world must know is an atomic flag, emitted by the protocol thread's coalescing
timer.

After a successful `SetCursorPos` the landing point must immediately be recorded
as "the position the hook last saw", or the watchdog mistakes the plugin's own
wrap for a dead hook.

## Debugging boundary

Any investigation that moves the real cursor or installs a real global hook —
including starting `cursorwrap.exe` directly as a probe — **requires the user's
consent first**. Building, unit tests, the protocol round trip and static
validation do not.

Validation evidence follows the workspace convention
(`winshun-dev/DEVELOPMENT.md`): curated evidence and machine-readable results
only inside this repo (≤5 MB), raw material stays out; every validation README
must include a "not tested" section.

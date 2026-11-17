# cursorwrap: search actions and tuning window plan

[中文](sdk-upgrade-plan.md)

## Why change

The search interface exposes all fourteen settings through many submenus. That is useful for direct commands but makes routine use and deliberate calibration compete for the same space. Numeric thresholds benefit from an explanation beside the existing measurement visualization.

The plugin already writes settings back to the host. Retain that working direction and replace the old SDK method name when updating the header.

`src/main.cpp::query()` currently calls `applyCommand()` for setting commands, and a tuning command also changes state during query. Typing can therefore alter live behavior before the user executes an action. Queries must present the proposed change; Enter applies it.

## User-facing scheme

Search home keeps wrapping state, direction, impact mode, tuning-window entry and data-window entry. Existing direct numeric and activation commands remain available. A direct command displays the intended value and an Apply action; repeated queries do not apply it.

The host card keeps direction, activation mode, no-wrap-during-drag, single-monitor suppression and impact mode. The plugin's existing runtime enable state remains distinct from enabling/disabling the entire plugin; this change does not invent a second persistent enable setting.

A dedicated settings window provides:

- Common direction, activation and suppression controls.
- Impact speed, acceleration and measuring-band controls.
- Edge threshold, anti-oscillation guard and adjacency tolerance.
- Tuning overlay, impact glow and data-window controls.
- Existing measurement summaries, suggested gate and a deliberate Apply suggested value action.

Advanced parameters removed from the host card become plugin-owned. Search numeric commands and the tuning window share that same configuration.

## Implementation

1. Update the SDK and use `updateSettings()` for host-owned values.
2. Separate parsing/proposed search changes from mutation. Apply, reset, tuner and suggestion actions execute only from `action()`.
3. Add a dedicated settings window using the current native UI approach, with `settingsWindow: true` and `showSettings()`. Keep the 64 MB runtime budget in view; adding a Qt runtime solely for settings is not required.
4. Reuse the settings field definitions and validation rules for window controls and command actions. Route committed changes through one update path, then update the engine, hook and related windows.
5. Add a store for advanced values and preserve previously configured values before manifest removal. Do not reuse legacy override precedence that can defeat a newer host edit.
6. Keep writes, notifications and window work outside the low-level mouse hook. UI updates follow the existing safe thread handoff.
7. Preserve current numeric ranges and accepted values. The acceleration parameter has a special off value and a separate nonzero range; show an off switch and threshold input rather than snapping off or valid values through an incompatible slider step.

The direction, speed measurement window, hard band boundary and acceleration semantics remain unchanged. This plan changes how users configure those behaviors.

## Acceptance

- Querying a numeric command, reset or tuning toggle leaves settings and window state unchanged until its action is executed.
- Host edits and window edits agree for common settings and survive restart.
- Advanced values and arbitrary currently valid numeric values survive restart and remain correctly displayed.
- Applying one threshold does not reset unrelated parameters.
- The suggested value requires an explicit action and updates the same value used by the engine.
- Closing or reopening the settings window does not alter wrapping state.
- Existing parser, settings and affected protocol tests cover the new action semantics.
- UI and behavior verification use `cw_devshell`; real hooks, pointer motion and feel checks follow the repository's explicit authorization rule.

## Documentation

Update the command reference to distinguish preview from Apply. Document the smaller search home, the advanced settings window and measurement units. Replace obsolete descriptions of search overrides with the implemented ownership rules.

// settings_store.h — persistence, split by who owns each setting.
//
// TWO OWNERS, ONE TABLE
// ---------------------
// The host card owns the everyday switches (wrapMode, activationMode,
// disableDuringDrag, disableOnSingleMonitor, impactGate): they are declared in
// plugin.json, the host saves them in WinShun.ini, and the plugin hears of
// changes through settingsChanged. The plugin pushes them back through
// updateSettings() (winshun/settings/update); the host stores them and answers
// with a settingsChanged carrying the merged values.
//
// The advanced tuning keys (speed, band, acceleration, guard, edge, tolerance,
// the two windows and the glow) are the plugin's own: they live in
// WS_DATA_DIR/settings.json as a minimal diff of non-default values, applied
// on top of the host's settings at startup. The host can no longer change them
// (they are not declared), and its settingsChanged payloads are filtered to
// the declared keys so a stale value left in WinShun.ini from an older version
// cannot silently restore itself over the user's newer tuning.
//
// Every persisted setting is still described exactly once, in settingsFields()
// — now with an owner. The dumper, the reader and the exe's merge paths all
// walk that table, so a field cannot be written by one path and forgotten by
// another. That is not a style preference: the plugin used to write 13 keys
// and read back 9, so "accel 2000" survived in settings.json and was silently
// dropped on the next start. A hand-maintained list in six places caused it,
// and only a single table prevents the same bug for the next setting.
#pragma once

#include "wrap_logic.h"
#include "winshun.h"

#include <cstddef>
#include <string>
#include <vector>

namespace cw {

// Who owns a setting and therefore who persists it.
enum class FieldOwner {
    Host,   // declared in plugin.json; saved by the host (WinShun.ini)
    Plugin, // the plugin's own; saved in WS_DATA_DIR/settings.json
};

// One persisted setting. `key` is the JSON name (the host card's key for
// owner Host); `isSet` reports whether the value differs from the defaults.
//
// `kind` exists so the JSON -> RawSettings mapping can be driven by the table
// instead of a second hand-written list.
enum class FieldKind { Text, Bool, Number };

struct SettingsField {
    const char* key;
    FieldKind kind;
    FieldOwner owner;
    // True when this field is present (non-default) in `s`.
    bool (*isSet)(const Settings& s);
    // The value as the storage spells it. Only the member the kind names is
    // meaningful.
    long long (*number)(const Settings& s);
    bool (*boolean)(const Settings& s);
    std::string (*text)(const Settings& s);
};

// The single source of truth for what is persisted and by whom. Order is the
// order the dumper writes; the reader accepts any order.
const std::vector<SettingsField>& settingsFields();

// The plugin file's shape: {"version":1,"settings":{...only changed keys...}}.
// Pure: returns the plugin-owned overrides that parse; host-card keys in an
// old file are ignored (they belong to the host now), and every bad value is
// dropped (the same clamps applySettings applies later).
Settings loadOverrides(const std::string& jsonText, bool& valid, std::string& problem);

// Only the plugin-owned keys that differ from `defaults` are written (a
// minimal diff, so an all-defaults tuning file is just deleted).
std::string dumpOverrides(const Settings& settings);

// A RawSettings built from the keys of `json` the table names. hostOnly
// restricts it to the host card's keys (settingsChanged); the full table is
// accepted when false (initialize, which may still carry pre-split values).
RawSettings rawOverridesFromJson(const winshun::Json& json, bool hostOnly);

// Which keys the override carries (for the status row's "来源/source" note).
bool overridesWrapMode(const Settings& o);
bool overridesActivation(const Settings& o);
bool overridesImpact(const Settings& o);

// The enum values as the files spell them ("both"/"vertical"/"horizontal",
// "always"/"ctrl"/"shift") — shared by the dumper and the exe's merge path.
std::string modeText(WrapMode m);
std::string activationText(ActivationMode a);

} // namespace cw

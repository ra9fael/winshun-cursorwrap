// settings_store.h — persistence for the changes made from the search bar.
//
// The host has no plugin→host settings channel (docs/plugins.md: a plugin
// can only send winshun/log|notify|progress|refresh|hotkeys|tool; unknown
// methods are dropped), and its WinShun.ini is the host's file. So a change
// typed in the search bar is kept in the plugin's own data directory —
// docs/plugins.md explicitly allows writing WS_DATA_DIR — and reapplied on
// top of the host's settings at startup, in settingsChanged, and before a
// query reads the status. A host-side change (Settings card) still wins
// unless the plugin has an override for that key; "reset" clears the file
// so the host card is authoritative again.
//
// Pure core: parse/dump work on strings, so the JSON round trip unit-tests
// without a disk. The file side (Win32 FILE*) lives in the exe.
//
// ONE TABLE, NOT SIX LISTS
// ------------------------
// Every persisted setting is described exactly once, in settingsFields(). The
// dumper, the reader and the exe's merge path all walk that table, so a field
// cannot be written by one path and forgotten by another. That is not a style
// preference: the plugin used to write 13 keys and read back 9, so
// "accel 2000" survived in settings.json and was silently dropped on the next
// start. A hand-maintained list in six places caused it, and only a single
// table prevents the same bug for the next setting that is added.
#pragma once

#include "wrap_logic.h"

#include <cstddef>
#include <string>
#include <vector>

namespace cw {

// One persisted setting. `key` is the JSON name (which is also the
// plugin.json key the host card uses); `isSet` reports whether the override
// file carries it; `write` adds it to a JSON object builder.
//
// `kind` exists so the exe can rebuild a RawSettings without a per-field
// switch of its own.
enum class FieldKind { Text, Bool, Number };

struct SettingsField {
    const char* key;
    FieldKind kind;
    // True when this field is present (non-default) in `s`.
    bool (*isSet)(const Settings& s);
    // The value as the override file stores it. Only the member the kind names
    // is meaningful.
    long long (*number)(const Settings& s);
    bool (*boolean)(const Settings& s);
    std::string (*text)(const Settings& s);
};

// The single source of truth for what is persisted. Order is the order the
// dumper writes; the reader accepts any order.
const std::vector<SettingsField>& settingsFields();

// The override file's shape: {"version":1,"settings":{...only changed keys...}}.
// Pure: returns the overrides that parse; every bad value is dropped (the
// same clamps applySettings applies later). `fallback` fills nothing — the
// result holds only the keys present in the file.
Settings loadOverrides(const std::string& jsonText, bool& valid, std::string& problem);

// Only the keys that differ from `defaults` are written (a minimal diff, so
// "reset" is just deleting the file and the host card stays authoritative).
std::string dumpOverrides(const Settings& settings);

// Which keys the override carries (for the status row's "来源/source" note).
bool overridesWrapMode(const Settings& o);
bool overridesActivation(const Settings& o);
bool overridesImpact(const Settings& o);

// The enum values as the file spells them ("both"/"vertical"/"horizontal",
// "always"/"ctrl"/"shift") — shared by the dumper and the exe's merge path.
std::string modeText(WrapMode m);
std::string activationText(ActivationMode a);

} // namespace cw

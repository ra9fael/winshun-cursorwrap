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
#pragma once

#include "wrap_logic.h"

#include <string>

namespace cw {

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

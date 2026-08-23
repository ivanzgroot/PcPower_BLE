// Settings <-> NVS (namespace "cfg").
#pragma once
#include "core/settings_model.h"

namespace SettingsStore {
void load(core::Settings& s);        // defaults first, then whatever NVS holds, all clamped
void save(const core::Settings& s);  // writes every key
void saveKey(const core::Settings& s, core::SettingId id);
void restoreDefaults(core::Settings& s);  // clears "cfg" only - devices and WiFi survive
}  // namespace SettingsStore

#include "settings_store.h"

#include <Preferences.h>

#include "app.h"

static const char* kNamespace = "cfg";

namespace SettingsStore {

void load(core::Settings& s) {
  s.loadDefaults();

  Preferences prefs;
  if (!prefs.begin(kNamespace, /*readOnly=*/true)) return;  // nothing stored yet

  for (uint8_t i = 0; i < core::S_NUM_SETTINGS; ++i) {
    const core::SettingDef& d = core::kSettingDefs[i];
    if (!prefs.isKey(d.key)) continue;
    if (d.type == core::SettingType::Str) {
      char buf[core::kStrLen];
      const size_t n = prefs.getString(d.key, buf, sizeof buf);
      if (n == 0 && d.min > 0) continue;  // stored empty where empty is not allowed
      char err[80];
      s.setStr((core::SettingId)i, buf, err, sizeof err);
    } else {
      // Feed stored values back through setNum so anything out of range is clamped rather
      // than trusted - a corrupted NVS entry must not be able to configure a 10 minute pulse.
      s.setNum((core::SettingId)i, prefs.getInt(d.key, d.def));
    }
  }
  prefs.end();
}

void save(const core::Settings& s) {
  Preferences prefs;
  if (!prefs.begin(kNamespace, /*readOnly=*/false)) {
    appLog("settings: could not open NVS for writing");
    return;
  }
  for (uint8_t i = 0; i < core::S_NUM_SETTINGS; ++i) {
    const core::SettingDef& d = core::kSettingDefs[i];
    if (d.type == core::SettingType::Str) {
      prefs.putString(d.key, s.str((core::SettingId)i));
    } else {
      prefs.putInt(d.key, s.num((core::SettingId)i));
    }
  }
  prefs.end();
}

void saveKey(const core::Settings& s, core::SettingId id) {
  Preferences prefs;
  if (!prefs.begin(kNamespace, /*readOnly=*/false)) return;
  const core::SettingDef& d = core::kSettingDefs[id];
  if (d.type == core::SettingType::Str) {
    prefs.putString(d.key, s.str(id));
  } else {
    prefs.putInt(d.key, s.num(id));
  }
  prefs.end();
}

void restoreDefaults(core::Settings& s) {
  Preferences prefs;
  if (prefs.begin(kNamespace, /*readOnly=*/false)) {
    prefs.clear();
    prefs.end();
  }
  s.loadDefaults();
  appLog("settings: restored defaults (devices and WiFi kept)");
}

}  // namespace SettingsStore

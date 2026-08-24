#include "settings_model.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <strings.h>  // strcasecmp

#include "json_out.h"

namespace core {

#define B SettingType::Bool
#define I SettingType::Int
#define E SettingType::Enum
#define S SettingType::Str

const SettingDef kSettingDefs[S_NUM_SETTINGS] = {
    {"pin_out", I, 0, 21, 5, nullptr, "Power button pin", "GPIO", nullptr, "Pins",
     "Drives the opto-isolator wired across the PC power button."},
    {"out_active_high", B, 0, 1, 1, nullptr, "Output active high", "", nullptr, "Pins",
     "On when high. Turn off if your opto-isolator is wired the other way."},
    {"pin_sense", I, 0, 21, 3, nullptr, "PWR-LED sense pin", "GPIO", nullptr, "Pins",
     "Reads the PC power LED through the second opto-isolator."},
    {"sense_act_low", B, 0, 1, 1, nullptr, "Sense active low", "", nullptr, "Pins",
     "On means the pin is pulled low while the PC LED is lit - correct for the G3VM-61A1."},
    {"pin_led", I, 0, 21, 8, nullptr, "Status LED pin", "GPIO", nullptr, "Pins",
     "The on-board LED of the ESP32-C3 SuperMini is GPIO8."},
    {"led_active_low", B, 0, 1, 1, nullptr, "Status LED active low", "", nullptr, "Pins",
     "The SuperMini's on-board LED lights when the pin is low."},
    {"led_enabled", B, 0, 1, 1, nullptr, "Status LED enabled", "", nullptr, "Pins",
     "Turn off to keep the board dark."},

    {"pulse_ms", I, 50, 2000, 300, nullptr, "Pulse length", "ms", nullptr, "Power",
     "How long the power button is held for a normal press."},
    {"long_press_ms", I, 1000, 15000, 6000, nullptr, "Long press length", "ms", nullptr, "Power",
     "Used by the force-off button. Most PCs cut power after 4-6 seconds."},

    {"scan_intvl_ms", I, 50, 1000, 200, nullptr, "Scan interval", "ms", nullptr, "Scanning",
     "How often the radio starts a scan window."},
    {"scan_window_ms", I, 10, 1000, 60, nullptr, "Scan window", "ms", nullptr, "Scanning",
     "How long it listens each interval. 60 of 200 ms is a 30% duty cycle."},
    {"scan_active", B, 0, 1, 1, nullptr, "Active scanning", "", nullptr, "Scanning",
     "Requests scan responses, which carry device names. Costs a little more airtime."},
    {"pause_when_on", B, 0, 1, 1, nullptr, "Pause scan while PC is on", "", nullptr, "Scanning",
     "Nothing can trigger while the PC runs, so the antenna goes to WiFi instead."},

    {"cooldown_ms", I, 0, 600000, 10000, nullptr, "Cooldown after a press", "ms", nullptr,
     "Guards", "Stops one advertising burst from pressing the button twice."},
    {"postoff_ms", I, 0, 600000, 30000, nullptr, "Block after shutdown", "ms", nullptr, "Guards",
     "A controller that just lost its host looks exactly like one being switched on."},
    {"require_absence", B, 0, 1, 0, nullptr, "Require the device to disappear first", "", nullptr,
     "Guards",
     "Turn on if your controller keeps advertising after the PC shuts down - it must then go "
     "unseen for the time below before it can wake the PC again."},
    {"absence_ms", I, 5000, 3600000, 60000, nullptr, "Absence needed to re-arm", "ms", nullptr,
     "Guards", "Only used when the setting above is on."},
    {"rssi_min", I, -100, -20, -100, nullptr, "Minimum signal to trigger", "dBm", nullptr,
     "Guards", "-100 disables the check. Raise it to ignore devices in the next room."},

    {"sense_mode", E, 0, 1, 0, nullptr, "Sense mode", "", "auto|force_off", "PC sense",
     "auto reads the PWR-LED. force_off assumes the PC is always off - use it if no sense "
     "wire is fitted."},
    {"sleep_is_off", B, 0, 1, 1, nullptr, "Treat sleep as off", "", nullptr, "PC sense",
     "On means a sleeping PC gets woken. Turn off to leave a sleeping PC alone."},
    {"sense_window_ms", I, 500, 10000, 2000, nullptr, "Sense window", "ms", nullptr, "PC sense",
     "How much history each decision looks at."},
    {"on_duty_pct", I, 50, 100, 95, nullptr, "On threshold", "%", nullptr, "PC sense",
     "Lit for at least this much of the window means the PC is on."},
    {"off_duty_pct", I, 0, 40, 2, nullptr, "Off threshold", "%", nullptr, "PC sense",
     "Lit for at most this much means the PC is off."},
    {"spread_pct", I, 5, 90, 35, nullptr, "Pulsing threshold", "%", nullptr, "PC sense",
     "Between the two thresholds: brightness steadier than this is a dimmed LED (on), more "
     "variable is a pulsing LED (sleep)."},
    {"off_confirm", I, 1, 100, 12, nullptr, "Confirmations before believing off", "", nullptr,
     "PC sense",
     "A false 'off' would press the button on a running PC, so leaving the on state is "
     "deliberately slow."},
    {"on_fast_ms", I, 50, 3000, 300, nullptr, "Instant-on light", "ms", nullptr, "PC sense",
     "Unbroken light for this long switches to 'on' straight away."},

    {"ap_timeout_s", I, 0, 3600, 60, nullptr, "Close hotspot after", "s", nullptr, "Network",
     "How long the hotspot stays up once the board has joined your WiFi. It lingers so you can "
     "confirm the connection worked before your phone drops back. 0 keeps it up permanently."},

    {"hostname", S, 1, 32, 0, "pcpower", "Hostname", "", nullptr, "Network",
     "Used for mDNS, so the board answers at http://<hostname>.local/"},
    {"ap_ssid", S, 0, 31, 0, "", "Hotspot name", "", nullptr, "Network",
     "Leave empty to use PcPower-XXXX built from this board's MAC address."},
    {"ap_pass", S, 8, 63, 0, "123454321", "Hotspot password", "", nullptr, "Network",
     "WPA2 requires at least 8 characters."},
};

#undef B
#undef I
#undef E
#undef S

// ---------------------------------------------------------------------------

void Settings::loadDefaults() {
  for (uint8_t i = 0; i < S_NUM_SETTINGS; ++i) {
    const SettingDef& d = kSettingDefs[i];
    if (d.type == SettingType::Str) {
      std::snprintf(str_[i - kFirstStringId], kStrLen, "%s", d.def_str ? d.def_str : "");
    } else {
      num_[i] = d.def;
    }
  }
}

int32_t Settings::num(SettingId id) const {
  if (id >= kFirstStringId) return 0;
  return num_[id];
}

const char* Settings::str(SettingId id) const {
  if (id < kFirstStringId || id >= S_NUM_SETTINGS) return "";
  return str_[id - kFirstStringId];
}

bool Settings::setNum(SettingId id, int32_t value, bool* clamped) {
  if (id >= kFirstStringId) return false;
  const SettingDef& d = kSettingDefs[id];
  int32_t v = value;
  if (v < d.min) v = d.min;
  if (v > d.max) v = d.max;
  if (clamped) *clamped = (v != value);
  num_[id] = v;
  return true;
}

bool Settings::setStr(SettingId id, const char* value, char* err, size_t err_len) {
  if (id < kFirstStringId || id >= S_NUM_SETTINGS) {
    if (err) std::snprintf(err, err_len, "not a text setting");
    return false;
  }
  const SettingDef& d = kSettingDefs[id];
  const size_t n = value ? std::strlen(value) : 0;
  if (n < (size_t)d.min || n > (size_t)d.max) {
    if (err)
      std::snprintf(err, err_len, "%s must be %d-%d characters", d.key, (int)d.min, (int)d.max);
    return false;
  }
  if (n >= kStrLen) {
    if (err) std::snprintf(err, err_len, "%s is too long", d.key);
    return false;
  }
  std::snprintf(str_[id - kFirstStringId], kStrLen, "%s", value ? value : "");
  return true;
}

const SettingDef* Settings::findDef(const char* key, SettingId* id_out) {
  if (!key) return nullptr;
  for (uint8_t i = 0; i < S_NUM_SETTINGS; ++i) {
    if (std::strcmp(kSettingDefs[i].key, key) == 0) {
      if (id_out) *id_out = (SettingId)i;
      return &kSettingDefs[i];
    }
  }
  return nullptr;
}

static bool parseBool(const char* v, int32_t* out) {
  static const char* kTrue[] = {"1", "true", "on", "yes"};
  static const char* kFalse[] = {"0", "false", "off", "no"};
  for (const char* t : kTrue)
    if (strcasecmp(v, t) == 0) { *out = 1; return true; }
  for (const char* f : kFalse)
    if (strcasecmp(v, f) == 0) { *out = 0; return true; }
  return false;
}

// "auto|force_off" -> index of the matching option, or -1.
static int optionIndex(const char* options, const char* value) {
  if (!options) return -1;
  int index = 0;
  const char* start = options;
  for (const char* p = options;; ++p) {
    if (*p == '|' || *p == '\0') {
      const size_t len = (size_t)(p - start);
      if (std::strlen(value) == len && std::strncmp(start, value, len) == 0) return index;
      if (*p == '\0') return -1;
      ++index;
      start = p + 1;
    }
  }
}

static bool parseInt(const char* v, int32_t* out) {
  if (!v || *v == '\0') return false;
  char* end = nullptr;
  const long parsed = std::strtol(v, &end, 10);
  if (end == v || (end && *end != '\0')) return false;
  *out = (int32_t)parsed;
  return true;
}

bool Settings::setByKey(const char* key, const char* value, char* err, size_t err_len,
                        bool* clamped) {
  SettingId id;
  const SettingDef* d = findDef(key, &id);
  if (!d) {
    if (err) std::snprintf(err, err_len, "unknown setting '%s'", key ? key : "");
    return false;
  }
  if (!value) {
    if (err) std::snprintf(err, err_len, "%s needs a value", d->key);
    return false;
  }

  switch (d->type) {
    case SettingType::Str:
      return setStr(id, value, err, err_len);

    case SettingType::Bool: {
      int32_t v;
      if (!parseBool(value, &v)) {
        if (err) std::snprintf(err, err_len, "%s must be true or false", d->key);
        return false;
      }
      return setNum(id, v, clamped);
    }

    case SettingType::Enum: {
      int32_t v;
      const int index = optionIndex(d->options, value);
      if (index >= 0) {
        v = index;
      } else if (!parseInt(value, &v)) {
        if (err) std::snprintf(err, err_len, "%s must be one of %s", d->key, d->options);
        return false;
      }
      if (v < d->min || v > d->max) {
        if (err) std::snprintf(err, err_len, "%s must be one of %s", d->key, d->options);
        return false;
      }
      return setNum(id, v, clamped);
    }

    case SettingType::Int: {
      int32_t v;
      if (!parseInt(value, &v)) {
        if (err) std::snprintf(err, err_len, "%s must be a whole number", d->key);
        return false;
      }
      return setNum(id, v, clamped);
    }
  }
  return false;
}

void Settings::applyCoupling(bool wifi_connected) {
  const int32_t interval = num_[S_SCAN_INTVL_MS];
  int32_t window = num_[S_SCAN_WINDOW_MS];
  int32_t ceiling = interval;
  if (wifi_connected) {
    // BLE and WiFi share one antenna on the C3. Above roughly 60% duty the web UI stalls.
    ceiling = interval * 60 / 100;
  }
  if (ceiling < kSettingDefs[S_SCAN_WINDOW_MS].min) ceiling = kSettingDefs[S_SCAN_WINDOW_MS].min;
  if (window > ceiling) window = ceiling;
  num_[S_SCAN_WINDOW_MS] = window;
}

// --- serialisation ---------------------------------------------------------

size_t Settings::toJson(char* buf, size_t len) const {
  size_t pos = 0;
  jsonAppend(buf, len, &pos, "{");
  for (uint8_t i = 0; i < S_NUM_SETTINGS; ++i) {
    const SettingDef& d = kSettingDefs[i];
    jsonAppend(buf, len, &pos, "%s\"%s\":", i ? "," : "", d.key);
    if (d.type == SettingType::Str) {
      jsonAppend(buf, len, &pos, "\"");
      jsonAppendEscaped(buf, len, &pos, str_[i - kFirstStringId]);
      jsonAppend(buf, len, &pos, "\"");
    } else {
      jsonAppend(buf, len, &pos, "%d", (int)num_[i]);
    }
  }
  jsonAppend(buf, len, &pos, "}");
  return pos;
}

size_t Settings::schemaToJson(char* buf, size_t len) {
  size_t pos = 0;
  jsonAppend(buf, len, &pos, "[");
  for (uint8_t i = 0; i < S_NUM_SETTINGS; ++i) {
    const SettingDef& d = kSettingDefs[i];
    const char* type = d.type == SettingType::Bool   ? "bool"
                       : d.type == SettingType::Int  ? "int"
                       : d.type == SettingType::Enum ? "enum"
                                                     : "str";
    jsonAppend(buf, len, &pos, "%s{\"key\":\"%s\",\"type\":\"%s\",\"min\":%d,\"max\":%d,", i ? "," : "",
            d.key, type, (int)d.min, (int)d.max);
    jsonAppend(buf, len, &pos, "\"label\":\"");
    jsonAppendEscaped(buf, len, &pos, d.label);
    jsonAppend(buf, len, &pos, "\",\"unit\":\"%s\",\"group\":\"%s\",\"options\":", d.unit, d.group);
    if (d.options) {
      jsonAppend(buf, len, &pos, "\"%s\"", d.options);
    } else {
      jsonAppend(buf, len, &pos, "null");
    }
    jsonAppend(buf, len, &pos, ",\"help\":\"");
    jsonAppendEscaped(buf, len, &pos, d.help);
    jsonAppend(buf, len, &pos, "\"}");
  }
  jsonAppend(buf, len, &pos, "]");
  return pos;
}

size_t Settings::toConf(char* buf, size_t len) const {
  size_t pos = 0;
  jsonAppend(buf, len, &pos, "# PcPower_BLE settings\n");
  for (uint8_t i = 0; i < S_NUM_SETTINGS; ++i) {
    const SettingDef& d = kSettingDefs[i];
    if (d.type == SettingType::Str) {
      jsonAppend(buf, len, &pos, "%s=%s\n", d.key, str_[i - kFirstStringId]);
    } else {
      jsonAppend(buf, len, &pos, "%s=%d\n", d.key, (int)num_[i]);
    }
  }
  return pos;
}

static char* trim(char* s) {
  while (*s == ' ' || *s == '\t') ++s;
  char* end = s + std::strlen(s);
  while (end > s && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r')) --end;
  *end = '\0';
  return s;
}

int Settings::applyConf(const char* text, char* err, size_t err_len) {
  if (!text) {
    if (err) std::snprintf(err, err_len, "empty configuration");
    return -1;
  }
  int applied = 0;
  const char* p = text;
  char line[160];
  while (*p) {
    const char* nl = std::strchr(p, '\n');
    const size_t n = nl ? (size_t)(nl - p) : std::strlen(p);
    const size_t copy = n < sizeof line - 1 ? n : sizeof line - 1;
    std::memcpy(line, p, copy);
    line[copy] = '\0';
    p += n + (nl ? 1 : 0);

    char* l = trim(line);
    if (*l == '\0' || *l == '#') continue;
    char* eq = std::strchr(l, '=');
    if (!eq) continue;
    *eq = '\0';
    char* key = trim(l);
    char* value = trim(eq + 1);
    if (setByKey(key, value, err, err_len)) ++applied;
  }
  return applied;
}

}  // namespace core

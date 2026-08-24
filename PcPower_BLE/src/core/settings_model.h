// Every runtime setting is described once, in kSettingDefs.
//
// NVS storage, the JSON the API returns, the form the web UI renders and the `set` console
// command all read that table, so adding a setting later means adding one row and nothing else.
// Keys are <= 15 characters because that is the NVS key limit.
#pragma once
#include <cstddef>
#include <cstdint>

namespace core {

enum SettingId : uint8_t {
  // Pins
  S_PIN_OUT, S_OUT_ACTIVE_HIGH, S_PIN_SENSE, S_SENSE_ACT_LOW,
  S_PIN_LED, S_LED_ACTIVE_LOW, S_LED_ENABLED,
  // Power button
  S_PULSE_MS, S_LONG_PRESS_MS,
  // Scanning
  S_SCAN_INTVL_MS, S_SCAN_WINDOW_MS, S_SCAN_ACTIVE, S_PAUSE_WHEN_ON,
  // Radio arbitration
  S_RADIO_EXCLUSIVE, S_WIFI_TRIES, S_WIFI_WINDOW_S, S_WIFI_RETRY_S,
  // Guards
  S_COOLDOWN_MS, S_POSTOFF_MS, S_REQUIRE_ABSENCE, S_ABSENCE_MS, S_RSSI_MIN,
  // PC sense
  S_SENSE_MODE, S_SLEEP_IS_OFF, S_SENSE_WINDOW_MS, S_ON_DUTY_PCT, S_OFF_DUTY_PCT,
  S_SPREAD_PCT, S_OFF_CONFIRM, S_ON_FAST_MS,
  // Network
  S_AP_TIMEOUT_S,
  // Strings, and they must stay last and contiguous
  S_HOSTNAME, S_AP_SSID, S_AP_PASS,
  S_NUM_SETTINGS
};

static constexpr uint8_t kFirstStringId = S_HOSTNAME;
static constexpr uint8_t kNumStrings = S_NUM_SETTINGS - kFirstStringId;
static constexpr size_t kStrLen = 64;

enum class SettingType : uint8_t { Bool, Int, Enum, Str };

struct SettingDef {
  const char* key;        // NVS key and API name
  SettingType type;
  int32_t min, max, def;  // for Str, min/max are length bounds and def is unused
  const char* def_str;    // Str only
  const char* label;      // human name for the UI
  const char* unit;       // "ms", "%", "dBm", ""
  const char* options;    // Enum only, e.g. "auto|force_off"
  const char* group;      // UI grouping
  const char* help;       // one sentence shown under the field
};

extern const SettingDef kSettingDefs[S_NUM_SETTINGS];

enum class SenseMode : int32_t { Auto = 0, ForceOff = 1 };

class Settings {
 public:
  Settings() { loadDefaults(); }

  void loadDefaults();

  int32_t num(SettingId id) const;
  bool flag(SettingId id) const { return num(id) != 0; }
  const char* str(SettingId id) const;

  // Clamps into range rather than rejecting; sets *clamped when the value had to move.
  // Returns false only when `id` is the wrong type.
  bool setNum(SettingId id, int32_t value, bool* clamped = nullptr);
  bool setStr(SettingId id, const char* value, char* err, size_t err_len);

  // Accepts "1"/"0"/"true"/"false"/"on"/"off"/"yes"/"no" for Bool, and either the option
  // name or its index for Enum. Never mutates when parsing fails.
  bool setByKey(const char* key, const char* value, char* err, size_t err_len,
                bool* clamped = nullptr);

  // The only invariant worth writing back: a scan window longer than its interval is
  // meaningless. How much duty cycle to yield to WiFi depends on whether WiFi is even running,
  // so that lives in effectiveScanWindowMs() and never overwrites what the user configured.
  void applyCoupling();

  static const SettingDef* findDef(const char* key, SettingId* id_out);

  size_t toJson(char* buf, size_t len) const;         // {"pin_out":5,...}
  static size_t schemaToJson(char* buf, size_t len);  // [{"key":...,"min":...}] - drives the UI
  size_t toConf(char* buf, size_t len) const;         // key=value lines, for export
  int applyConf(const char* text, char* err, size_t err_len);  // returns settings applied

 private:
  int32_t num_[kFirstStringId] = {};
  char str_[kNumStrings][kStrLen] = {};
};

}  // namespace core

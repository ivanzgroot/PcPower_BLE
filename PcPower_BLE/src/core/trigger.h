// The guard set: everything that must be true before the power button is pressed.
//
// One pure function over a plain struct, so every branch is unit-tested and the BLE callback
// can call it without allocating anything.
#pragma once
#include <cstdint>

#include "led_classifier.h"

namespace core {

enum class TriggerReason : uint8_t {
  Fire,
  UnknownDevice,
  DeviceDisabled,
  PcOn,
  PostShutdownBlock,
  Cooldown,
  TooFar,
  NotRearmed,
  Inhibited,
};

const char* triggerReasonName(TriggerReason r);  // short slug for the API, e.g. "cooldown"
const char* triggerReasonText(TriggerReason r);  // sentence for the log and the UI

struct TriggerInputs {
  bool device_known = false;
  bool device_enabled = false;
  PcState pc_state = PcState::Unknown;
  uint32_t ms_since_pc_off_edge = UINT32_MAX;  // UINT32_MAX: the PC has never been seen on
  uint32_t ms_since_last_pulse = UINT32_MAX;   // UINT32_MAX: nothing has pulsed yet
  uint32_t device_absent_ms = UINT32_MAX;      // gap before this sighting
  int8_t rssi = 0;
  bool inhibited = false;                      // OTA running, or learning mode active
};

struct TriggerConfig {
  uint32_t postoff_block_ms = 30000;
  uint32_t cooldown_ms = 10000;
  bool require_absence = false;
  uint32_t absence_ms = 60000;
  int8_t rssi_min = -100;  // -100 disables the check
  bool sleep_is_off = true;
};

TriggerReason evaluate(const TriggerInputs& in, const TriggerConfig& cfg);
inline bool shouldFire(TriggerReason r) { return r == TriggerReason::Fire; }

}  // namespace core

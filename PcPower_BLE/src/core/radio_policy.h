// Who gets the antenna.
//
// The ESP32-C3 has one radio and one antenna shared between WiFi and Bluetooth. Running both
// costs each of them: the scan misses advertisements while WiFi transmits, and the web
// interface stalls while the scan listens. Since the two jobs are never needed at the same
// time - nothing can trigger a wake while the PC is already running, and nobody is browsing
// the settings page while the PC is off - exclusive mode hands the whole radio to whichever
// one is actually useful right now.
#pragma once
#include <cstdint>

#include "led_classifier.h"

namespace core {

enum class RadioOwner : uint8_t { Ble, WiFi };
const char* radioOwnerName(RadioOwner owner);

struct RadioConfig {
  bool exclusive = true;              // false reverts to running both at once
  bool pause_scan_when_pc_on = true;  // shared mode only
  bool sense_forced_off = false;      // sense_mode == force_off
  uint32_t dwell_ms = 5000;           // how long a change must hold before the radios switch
};

struct RadioInputs {
  PcState pc_state = PcState::Unknown;
  bool ota_in_progress = false;
  bool learning = false;  // a learning window needs the scanner regardless of PC state
};

struct RadioPlan {
  bool wifi_enabled = true;
  bool ble_scanning = true;
  RadioOwner owner = RadioOwner::Ble;
};

// The plan for a given moment, with no smoothing applied.
RadioPlan planFor(const RadioInputs& in, const RadioConfig& cfg);

// planFor with a dwell on the PC state, so a flickering sense reading cannot make the board
// tear WiFi up and down repeatedly. An update in flight bypasses the dwell entirely.
class RadioArbiter {
 public:
  void begin(uint32_t now_ms);
  RadioPlan update(const RadioInputs& in, const RadioConfig& cfg, uint32_t now_ms);
  bool settled() const { return pending_pc_on_ == pc_on_; }
  uint32_t lastSwitchMs() const { return last_switch_ms_; }

 private:
  bool pc_on_ = false;
  bool pending_pc_on_ = false;
  uint32_t pending_since_ms_ = 0;
  uint32_t last_switch_ms_ = 0;
};

// The scan window actually used. The configured value is never overwritten: yielding duty
// cycle to WiFi is only correct while the two radios are genuinely running together, which in
// exclusive mode they never are.
uint16_t effectiveScanWindowMs(int32_t interval_ms, int32_t window_ms, bool sharing_antenna);

// "Up to N attempts inside a window, then give up and raise the hotspot." Each attempt gets an
// equal slice of the window, which is how long it may sit there waiting for a router to answer.
class ConnectBudget {
 public:
  void begin(uint8_t max_attempts, uint32_t window_ms);
  void start(uint32_t now_ms);
  bool shouldAttempt(uint32_t now_ms) const;
  void recordAttempt(uint32_t now_ms);
  bool exhausted(uint32_t now_ms) const;
  uint8_t attempts() const { return attempts_; }
  uint32_t perAttemptMs() const;

 private:
  uint8_t max_attempts_ = 5;
  uint32_t window_ms_ = 60000;
  uint32_t start_ms_ = 0;
  uint32_t last_attempt_ms_ = 0;
  uint8_t attempts_ = 0;
};

}  // namespace core

// Bookkeeping for one power-button press.
//
// Pure timing arithmetic so it can be tested natively; the Arduino layer owns the actual pin
// and an esp_timer that releases it. All comparisons are unsigned differences, so a millis()
// wraparound at 49 days cannot stretch a 300 ms press into a 49-day one.
#pragma once
#include <cstdint>

namespace core {

class PulseTimer {
 public:
  void begin(uint32_t max_ms);                        // hard ceiling on any single pulse
  bool start(uint32_t now_ms, uint32_t duration_ms);  // false if one is already running
  bool active() const { return active_; }
  bool update(uint32_t now_ms);  // true exactly once, on the pulse's finishing edge
  uint32_t remaining(uint32_t now_ms) const;
  uint32_t startedAt() const { return start_ms_; }
  uint32_t lastFinishedAt() const { return finished_ms_; }
  void abort();

 private:
  uint32_t max_ms_ = 15000;
  uint32_t start_ms_ = 0;
  uint32_t duration_ms_ = 0;
  uint32_t finished_ms_ = 0;
  bool active_ = false;
};

}  // namespace core

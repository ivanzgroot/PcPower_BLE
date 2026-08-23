#include "core/pulse_timer.h"

namespace core {

void PulseTimer::begin(uint32_t max_ms) {
  max_ms_ = max_ms;
  active_ = false;
}

bool PulseTimer::start(uint32_t now_ms, uint32_t duration_ms) {
  if (active_) return false;
  if (duration_ms == 0) return false;
  if (duration_ms > max_ms_) duration_ms = max_ms_;
  start_ms_ = now_ms;
  duration_ms_ = duration_ms;
  active_ = true;
  return true;
}

bool PulseTimer::update(uint32_t now_ms) {
  if (!active_) return false;
  if ((uint32_t)(now_ms - start_ms_) < duration_ms_) return false;
  active_ = false;
  finished_ms_ = now_ms;
  return true;
}

uint32_t PulseTimer::remaining(uint32_t now_ms) const {
  if (!active_) return 0;
  const uint32_t elapsed = now_ms - start_ms_;
  return elapsed >= duration_ms_ ? 0 : duration_ms_ - elapsed;
}

void PulseTimer::abort() { active_ = false; }

}  // namespace core

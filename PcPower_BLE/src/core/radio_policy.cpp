#include "radio_policy.h"

namespace core {

const char* radioOwnerName(RadioOwner owner) {
  return owner == RadioOwner::WiFi ? "wifi" : "ble";
}

RadioPlan planFor(const RadioInputs& in, const RadioConfig& cfg) {
  RadioPlan plan;

  // Never pull WiFi out from under an update that is being written to flash.
  if (in.ota_in_progress) {
    plan.wifi_enabled = true;
    plan.ble_scanning = false;
    plan.owner = RadioOwner::WiFi;
    return plan;
  }

  const bool pc_on = in.pc_state == PcState::On;

  // With no sense wire the PC always reads as off, so exclusive mode would switch WiFi off and
  // never switch it back, leaving the serial console as the only way in. Share instead.
  const bool exclusive = cfg.exclusive && !cfg.sense_forced_off;

  if (exclusive) {
    plan.wifi_enabled = pc_on;
    plan.ble_scanning = !pc_on;
  } else {
    plan.wifi_enabled = true;
    plan.ble_scanning = !(pc_on && cfg.pause_scan_when_pc_on);
  }
  plan.owner = pc_on ? RadioOwner::WiFi : RadioOwner::Ble;
  return plan;
}

void RadioArbiter::begin(uint32_t now_ms) {
  pc_on_ = false;  // start by listening; waking the PC is the board's reason to exist
  pending_pc_on_ = false;
  pending_since_ms_ = now_ms;
  last_switch_ms_ = now_ms;
}

RadioPlan RadioArbiter::update(const RadioInputs& in, const RadioConfig& cfg, uint32_t now_ms) {
  if (in.ota_in_progress) return planFor(in, cfg);

  const bool raw_on = in.pc_state == PcState::On;
  if (raw_on != pending_pc_on_) {
    pending_pc_on_ = raw_on;
    pending_since_ms_ = now_ms;
  }
  if (pending_pc_on_ != pc_on_ && (uint32_t)(now_ms - pending_since_ms_) >= cfg.dwell_ms) {
    pc_on_ = pending_pc_on_;
    last_switch_ms_ = now_ms;
  }

  RadioInputs settled = in;
  settled.pc_state = pc_on_ ? PcState::On : PcState::Off;
  return planFor(settled, cfg);
}

void ConnectBudget::begin(uint8_t max_attempts, uint32_t window_ms) {
  max_attempts_ = max_attempts > 0 ? max_attempts : 1;
  window_ms_ = window_ms > 0 ? window_ms : 1;
  attempts_ = 0;
}

uint32_t ConnectBudget::perAttemptMs() const { return window_ms_ / max_attempts_; }

void ConnectBudget::start(uint32_t now_ms) {
  start_ms_ = now_ms;
  last_attempt_ms_ = now_ms;
  attempts_ = 0;
}

bool ConnectBudget::shouldAttempt(uint32_t now_ms) const {
  if (attempts_ >= max_attempts_) return false;
  if (attempts_ == 0) return true;
  return (uint32_t)(now_ms - last_attempt_ms_) >= perAttemptMs();
}

void ConnectBudget::recordAttempt(uint32_t now_ms) {
  if (attempts_ < 0xFF) ++attempts_;
  last_attempt_ms_ = now_ms;
}

bool ConnectBudget::exhausted(uint32_t now_ms) const {
  if ((uint32_t)(now_ms - start_ms_) >= window_ms_) return true;
  return attempts_ >= max_attempts_ &&
         (uint32_t)(now_ms - last_attempt_ms_) >= perAttemptMs();
}

}  // namespace core

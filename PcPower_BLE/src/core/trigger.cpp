#include "trigger.h"

namespace core {

const char* triggerReasonName(TriggerReason r) {
  switch (r) {
    case TriggerReason::Fire: return "fire";
    case TriggerReason::UnknownDevice: return "unknown_device";
    case TriggerReason::DeviceDisabled: return "device_disabled";
    case TriggerReason::PcOn: return "pc_on";
    case TriggerReason::PostShutdownBlock: return "post_shutdown_block";
    case TriggerReason::Cooldown: return "cooldown";
    case TriggerReason::TooFar: return "too_far";
    case TriggerReason::NotRearmed: return "not_rearmed";
    case TriggerReason::Inhibited: return "inhibited";
  }
  return "unknown";
}

const char* triggerReasonText(TriggerReason r) {
  switch (r) {
    case TriggerReason::Fire: return "pressed the power button";
    case TriggerReason::UnknownDevice: return "device is not in the known list";
    case TriggerReason::DeviceDisabled: return "device is switched off in the list";
    case TriggerReason::PcOn: return "the PC is already running";
    case TriggerReason::PostShutdownBlock:
      return "still inside the block after the PC shut down";
    case TriggerReason::Cooldown: return "still cooling down from the last press";
    case TriggerReason::TooFar: return "signal is weaker than the minimum";
    case TriggerReason::NotRearmed: return "device has not been away long enough to re-arm";
    case TriggerReason::Inhibited: return "triggering is paused";
  }
  return "unknown";
}

TriggerReason evaluate(const TriggerInputs& in, const TriggerConfig& cfg) {
  if (in.inhibited) return TriggerReason::Inhibited;
  if (!in.device_known) return TriggerReason::UnknownDevice;
  if (!in.device_enabled) return TriggerReason::DeviceDisabled;

  // Unknown counts as running. A sense wire that has not reported yet must never license a press.
  const bool pc_is_off = in.pc_state == PcState::Off ||
                         (in.pc_state == PcState::Sleep && cfg.sleep_is_off);
  if (!pc_is_off) return TriggerReason::PcOn;

  // A controller that just lost its host looks exactly like one being switched on.
  if (in.ms_since_pc_off_edge < cfg.postoff_block_ms) return TriggerReason::PostShutdownBlock;

  // One advertising burst must not press the button twice.
  if (in.ms_since_last_pulse < cfg.cooldown_ms) return TriggerReason::Cooldown;

  if (cfg.rssi_min > -100 && in.rssi < cfg.rssi_min) return TriggerReason::TooFar;

  if (cfg.require_absence && in.device_absent_ms < cfg.absence_ms) {
    return TriggerReason::NotRearmed;
  }

  return TriggerReason::Fire;
}

}  // namespace core

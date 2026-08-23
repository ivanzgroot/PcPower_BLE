#include "test_main.h"
#include "core/trigger.h"

// A sighting that should fire: known, enabled, PC off, nothing recent.
static core::TriggerInputs goodInputs() {
  core::TriggerInputs in;
  in.device_known = true;
  in.device_enabled = true;
  in.pc_state = core::PcState::Off;
  in.ms_since_pc_off_edge = 120000;
  in.ms_since_last_pulse = 120000;
  in.device_absent_ms = 120000;
  in.rssi = -55;
  in.inhibited = false;
  return in;
}

TEST(trigger_fires_when_every_guard_is_clear) {
  CHECK(core::evaluate(goodInputs(), core::TriggerConfig{}) == core::TriggerReason::Fire);
  CHECK(core::shouldFire(core::TriggerReason::Fire));
  CHECK(!core::shouldFire(core::TriggerReason::Cooldown));
}

TEST(trigger_refuses_unknown_and_disabled_devices) {
  core::TriggerInputs in = goodInputs();
  in.device_known = false;
  CHECK(core::evaluate(in, core::TriggerConfig{}) == core::TriggerReason::UnknownDevice);
  in.device_known = true;
  in.device_enabled = false;
  CHECK(core::evaluate(in, core::TriggerConfig{}) == core::TriggerReason::DeviceDisabled);
}

TEST(trigger_never_presses_while_the_pc_runs) {
  core::TriggerInputs in = goodInputs();
  in.pc_state = core::PcState::On;
  CHECK(core::evaluate(in, core::TriggerConfig{}) == core::TriggerReason::PcOn);
}

TEST(trigger_treats_unknown_pc_state_as_running) {
  core::TriggerInputs in = goodInputs();
  in.pc_state = core::PcState::Unknown;
  CHECK(core::evaluate(in, core::TriggerConfig{}) == core::TriggerReason::PcOn);
}

TEST(trigger_wakes_from_sleep_by_default) {
  core::TriggerInputs in = goodInputs();
  in.pc_state = core::PcState::Sleep;
  CHECK(core::evaluate(in, core::TriggerConfig{}) == core::TriggerReason::Fire);

  core::TriggerConfig cfg;
  cfg.sleep_is_off = false;
  CHECK(core::evaluate(in, cfg) == core::TriggerReason::PcOn);
}

TEST(trigger_holds_off_right_after_a_shutdown) {
  core::TriggerInputs in = goodInputs();
  in.ms_since_pc_off_edge = 5000;
  CHECK(core::evaluate(in, core::TriggerConfig{}) == core::TriggerReason::PostShutdownBlock);
  in.ms_since_pc_off_edge = 30000;  // the boundary is inclusive
  CHECK(core::evaluate(in, core::TriggerConfig{}) == core::TriggerReason::Fire);
}

TEST(trigger_never_blocks_when_the_pc_was_never_on) {
  core::TriggerInputs in = goodInputs();
  in.ms_since_pc_off_edge = UINT32_MAX;
  CHECK(core::evaluate(in, core::TriggerConfig{}) == core::TriggerReason::Fire);
}

TEST(trigger_cooldown_stops_a_double_press) {
  core::TriggerInputs in = goodInputs();
  in.ms_since_last_pulse = 2000;
  CHECK(core::evaluate(in, core::TriggerConfig{}) == core::TriggerReason::Cooldown);
  in.ms_since_last_pulse = 10000;
  CHECK(core::evaluate(in, core::TriggerConfig{}) == core::TriggerReason::Fire);
}

TEST(trigger_rssi_floor_is_off_by_default) {
  core::TriggerInputs in = goodInputs();
  in.rssi = -99;
  CHECK(core::evaluate(in, core::TriggerConfig{}) == core::TriggerReason::Fire);

  core::TriggerConfig cfg;
  cfg.rssi_min = -70;
  CHECK(core::evaluate(in, cfg) == core::TriggerReason::TooFar);
  in.rssi = -70;
  CHECK(core::evaluate(in, cfg) == core::TriggerReason::Fire);
}

TEST(trigger_absence_rearm_is_optional) {
  core::TriggerInputs in = goodInputs();
  in.device_absent_ms = 1000;  // seen a second ago
  CHECK(core::evaluate(in, core::TriggerConfig{}) == core::TriggerReason::Fire);  // off by default

  core::TriggerConfig cfg;
  cfg.require_absence = true;
  CHECK(core::evaluate(in, cfg) == core::TriggerReason::NotRearmed);
  in.device_absent_ms = 60000;
  CHECK(core::evaluate(in, cfg) == core::TriggerReason::Fire);
  in.device_absent_ms = UINT32_MAX;  // a first sighting counts as re-armed
  CHECK(core::evaluate(in, cfg) == core::TriggerReason::Fire);
}

TEST(trigger_inhibit_beats_everything) {
  core::TriggerInputs in = goodInputs();
  in.inhibited = true;
  in.device_known = false;
  CHECK(core::evaluate(in, core::TriggerConfig{}) == core::TriggerReason::Inhibited);
}

TEST(trigger_zero_guards_allow_immediate_retrigger) {
  core::TriggerInputs in = goodInputs();
  in.ms_since_pc_off_edge = 0;
  in.ms_since_last_pulse = 0;
  core::TriggerConfig cfg;
  cfg.postoff_block_ms = 0;
  cfg.cooldown_ms = 0;
  CHECK(core::evaluate(in, cfg) == core::TriggerReason::Fire);
}

TEST(trigger_every_reason_has_text) {
  const core::TriggerReason all[] = {
      core::TriggerReason::Fire,     core::TriggerReason::UnknownDevice,
      core::TriggerReason::DeviceDisabled, core::TriggerReason::PcOn,
      core::TriggerReason::PostShutdownBlock, core::TriggerReason::Cooldown,
      core::TriggerReason::TooFar,   core::TriggerReason::NotRearmed,
      core::TriggerReason::Inhibited};
  for (auto r : all) {
    CHECK(std::strlen(core::triggerReasonName(r)) > 0);
    CHECK(std::strlen(core::triggerReasonText(r)) > 0);
  }
}

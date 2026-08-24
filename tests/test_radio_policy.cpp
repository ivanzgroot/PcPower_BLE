#include "test_main.h"
#include "core/radio_policy.h"

static core::RadioConfig exclusiveCfg() {
  core::RadioConfig c;
  c.exclusive = true;
  c.pause_scan_when_pc_on = true;
  c.sense_forced_off = false;
  c.dwell_ms = 5000;
  return c;
}

static core::RadioInputs at(core::PcState s) {
  core::RadioInputs in;
  in.pc_state = s;
  return in;
}

// --- the plan ---------------------------------------------------------------

TEST(radio_exclusive_gives_the_antenna_to_ble_while_the_pc_is_off) {
  const core::RadioPlan p = core::planFor(at(core::PcState::Off), exclusiveCfg());
  CHECK(!p.wifi_enabled);
  CHECK(p.ble_scanning);
  CHECK(p.owner == core::RadioOwner::Ble);
}

TEST(radio_exclusive_treats_sleep_and_unknown_as_off) {
  for (auto s : {core::PcState::Sleep, core::PcState::Unknown}) {
    const core::RadioPlan p = core::planFor(at(s), exclusiveCfg());
    CHECK(!p.wifi_enabled);
    CHECK(p.ble_scanning);
  }
}

TEST(radio_exclusive_gives_the_antenna_to_wifi_while_the_pc_runs) {
  const core::RadioPlan p = core::planFor(at(core::PcState::On), exclusiveCfg());
  CHECK(p.wifi_enabled);
  CHECK(!p.ble_scanning);
  CHECK(p.owner == core::RadioOwner::WiFi);
}

TEST(radio_shared_mode_keeps_wifi_up_throughout) {
  core::RadioConfig cfg = exclusiveCfg();
  cfg.exclusive = false;

  core::RadioPlan p = core::planFor(at(core::PcState::Off), cfg);
  CHECK(p.wifi_enabled);
  CHECK(p.ble_scanning);

  p = core::planFor(at(core::PcState::On), cfg);
  CHECK(p.wifi_enabled);
  CHECK(!p.ble_scanning);          // pause_scan_when_pc_on still applies

  cfg.pause_scan_when_pc_on = false;
  p = core::planFor(at(core::PcState::On), cfg);
  CHECK(p.wifi_enabled);
  CHECK(p.ble_scanning);
}

TEST(radio_exclusive_stands_down_when_the_sense_is_forced_off) {
  // Without a sense wire the board can never see the PC as on, so exclusive mode would kill
  // WiFi permanently and leave serial as the only way in. Fall back to sharing instead.
  core::RadioConfig cfg = exclusiveCfg();
  cfg.sense_forced_off = true;
  const core::RadioPlan p = core::planFor(at(core::PcState::Off), cfg);
  CHECK(p.wifi_enabled);
  CHECK(p.ble_scanning);
}

TEST(radio_never_cuts_wifi_during_an_update) {
  core::RadioInputs in = at(core::PcState::Off);
  in.ota_in_progress = true;
  const core::RadioPlan p = core::planFor(in, exclusiveCfg());
  CHECK(p.wifi_enabled);
  CHECK(!p.ble_scanning);
  CHECK(p.owner == core::RadioOwner::WiFi);
}

TEST(radio_owner_has_a_name) {
  CHECK_STREQ(core::radioOwnerName(core::RadioOwner::Ble), "ble");
  CHECK_STREQ(core::radioOwnerName(core::RadioOwner::WiFi), "wifi");
}

// --- the dwell --------------------------------------------------------------

TEST(radio_arbiter_starts_on_ble) {
  core::RadioArbiter a;
  a.begin(0);
  const core::RadioPlan p = a.update(at(core::PcState::Unknown), exclusiveCfg(), 0);
  CHECK(p.owner == core::RadioOwner::Ble);
  CHECK(a.settled());
}

TEST(radio_arbiter_waits_out_the_dwell_before_switching) {
  core::RadioArbiter a;
  a.begin(0);
  a.update(at(core::PcState::Off), exclusiveCfg(), 0);

  core::RadioPlan p = a.update(at(core::PcState::On), exclusiveCfg(), 1000);
  CHECK(p.owner == core::RadioOwner::Ble);   // seen it, not acted on it
  CHECK(!a.settled());

  // The change was first seen at t=1000, so the dwell expires at 6000.
  p = a.update(at(core::PcState::On), exclusiveCfg(), 5999);
  CHECK(p.owner == core::RadioOwner::Ble);

  p = a.update(at(core::PcState::On), exclusiveCfg(), 6000);
  CHECK(p.owner == core::RadioOwner::WiFi);
  CHECK(a.settled());
  CHECK_EQ(a.lastSwitchMs(), 6000);
}

TEST(radio_arbiter_ignores_a_flicker_shorter_than_the_dwell) {
  core::RadioArbiter a;
  a.begin(0);
  a.update(at(core::PcState::Off), exclusiveCfg(), 0);
  a.update(at(core::PcState::On), exclusiveCfg(), 1000);
  a.update(at(core::PcState::Off), exclusiveCfg(), 3000);   // back before the dwell expired
  const core::RadioPlan p = a.update(at(core::PcState::Off), exclusiveCfg(), 9000);
  CHECK(p.owner == core::RadioOwner::Ble);                  // never switched
  CHECK(a.settled());
}

TEST(radio_arbiter_switches_back_after_the_dwell_too) {
  core::RadioArbiter a;
  a.begin(0);
  a.update(at(core::PcState::On), exclusiveCfg(), 0);
  a.update(at(core::PcState::On), exclusiveCfg(), 5000);
  CHECK(a.update(at(core::PcState::On), exclusiveCfg(), 6000).owner == core::RadioOwner::WiFi);

  a.update(at(core::PcState::Off), exclusiveCfg(), 7000);
  CHECK(a.update(at(core::PcState::Off), exclusiveCfg(), 11000).owner == core::RadioOwner::WiFi);
  CHECK(a.update(at(core::PcState::Off), exclusiveCfg(), 12000).owner == core::RadioOwner::Ble);
}

TEST(radio_arbiter_lets_an_update_through_without_waiting) {
  core::RadioArbiter a;
  a.begin(0);
  a.update(at(core::PcState::Off), exclusiveCfg(), 0);
  core::RadioInputs in = at(core::PcState::Off);
  in.ota_in_progress = true;
  const core::RadioPlan p = a.update(in, exclusiveCfg(), 100);
  CHECK(p.wifi_enabled);   // no dwell for an update in flight
}

// --- the connect budget -----------------------------------------------------

TEST(budget_divides_the_window_between_the_tries) {
  core::ConnectBudget b;
  b.begin(5, 60000);
  CHECK_EQ(b.perAttemptMs(), 12000);
  b.begin(1, 60000);
  CHECK_EQ(b.perAttemptMs(), 60000);
  b.begin(0, 60000);           // nonsense config must not divide by zero
  CHECK_EQ(b.perAttemptMs(), 60000);
}

TEST(budget_allows_the_first_attempt_at_once) {
  core::ConnectBudget b;
  b.begin(5, 60000);
  b.start(1000);
  CHECK(b.shouldAttempt(1000));
  CHECK(!b.exhausted(1000));
  CHECK_EQ(b.attempts(), 0);
}

TEST(budget_spaces_attempts_out) {
  core::ConnectBudget b;
  b.begin(5, 60000);
  b.start(0);
  b.recordAttempt(0);
  CHECK_EQ(b.attempts(), 1);
  CHECK(!b.shouldAttempt(5000));
  CHECK(!b.shouldAttempt(11999));
  CHECK(b.shouldAttempt(12000));
}

TEST(budget_runs_out_after_the_last_attempt_has_had_its_turn) {
  core::ConnectBudget b;
  b.begin(5, 60000);
  b.start(0);
  uint32_t t = 0;
  for (int i = 0; i < 5; ++i) {
    CHECK(b.shouldAttempt(t));
    b.recordAttempt(t);
    t += 12000;
  }
  CHECK_EQ(b.attempts(), 5);
  CHECK(!b.shouldAttempt(t));      // no tries left
  CHECK(b.exhausted(t));
}

TEST(budget_runs_out_when_the_window_elapses_even_with_tries_left) {
  core::ConnectBudget b;
  b.begin(5, 60000);
  b.start(0);
  b.recordAttempt(0);
  CHECK(!b.exhausted(59999));
  CHECK(b.exhausted(60000));
}

TEST(budget_restarts_cleanly) {
  core::ConnectBudget b;
  b.begin(5, 60000);
  b.start(0);
  for (uint32_t t = 0; t < 60000; t += 12000) b.recordAttempt(t);
  CHECK(b.exhausted(60000));
  b.start(100000);
  CHECK_EQ(b.attempts(), 0);
  CHECK(!b.exhausted(100000));
  CHECK(b.shouldAttempt(100000));
}

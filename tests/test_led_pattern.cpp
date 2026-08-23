#include "test_main.h"
#include "core/led_pattern.h"

// Lit fraction of a mode over `span_ms`, sampled every millisecond.
static int litPercent(core::LedMode mode, uint32_t span_ms) {
  uint32_t lit = 0;
  for (uint32_t t = 0; t < span_ms; ++t)
    if (core::ledLitAt(mode, t)) ++lit;
  return (int)(lit * 100 / span_ms);
}

// Rising edges over `span_ms`.
static int blinkCount(core::LedMode mode, uint32_t span_ms) {
  int edges = 0;
  bool prev = core::ledLitAt(mode, 0);
  if (prev) ++edges;  // count a blink that starts at t=0
  for (uint32_t t = 1; t < span_ms; ++t) {
    const bool now = core::ledLitAt(mode, t);
    if (now && !prev) ++edges;
    prev = now;
  }
  return edges;
}

TEST(led_boot_and_pulse_are_solid) {
  CHECK_EQ(litPercent(core::LedMode::Boot, 2000), 100);
  CHECK_EQ(litPercent(core::LedMode::PulseActive, 2000), 100);
}

TEST(led_dark_is_never_lit) { CHECK_EQ(litPercent(core::LedMode::Dark, 2000), 0); }

TEST(led_portal_double_blinks_every_two_seconds) {
  CHECK_EQ(blinkCount(core::LedMode::Portal, 6000), 6);  // 2 blinks x 3 periods
}

TEST(led_wifi_connecting_blinks_at_five_hertz) {
  CHECK_EQ(blinkCount(core::LedMode::WifiConnecting, 2000), 10);
}

TEST(led_armed_blinks_once_every_three_seconds) {
  CHECK_EQ(blinkCount(core::LedMode::ArmedScanning, 9000), 3);
  CHECK(litPercent(core::LedMode::ArmedScanning, 9000) <= 2);  // a brief wink
}

TEST(led_pc_on_blinks_once_every_eight_seconds) {
  CHECK_EQ(blinkCount(core::LedMode::PcOnIdle, 24000), 3);
}

TEST(led_learning_flickers_fast) { CHECK_EQ(blinkCount(core::LedMode::Learning, 1000), 10); }

TEST(led_every_mode_is_distinguishable) {
  const core::LedMode modes[] = {core::LedMode::Boot,          core::LedMode::Portal,
                                 core::LedMode::WifiConnecting, core::LedMode::ArmedScanning,
                                 core::LedMode::PcOnIdle,      core::LedMode::Learning};
  for (size_t i = 0; i < 6; ++i) {
    CHECK(std::strlen(core::ledModeName(modes[i])) > 0);
    for (size_t j = i + 1; j < 6; ++j) {
      const bool differs = blinkCount(modes[i], 24000) != blinkCount(modes[j], 24000) ||
                           litPercent(modes[i], 24000) != litPercent(modes[j], 24000);
      CHECK(differs);
    }
  }
}

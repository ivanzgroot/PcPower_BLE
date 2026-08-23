#include "test_main.h"
#include "core/led_classifier.h"

// Feeds `duration_ms` of samples at 2-4 ms with deliberate jitter, asking `pattern` whether the
// LED is lit at each instant. Mirrors what the firmware's sampling task does, and the jitter is
// the point: a fixed period can alias a PWM-dimmed LED into a constant reading.
template <typename Fn>
static uint32_t feed(core::LedClassifier& c, uint32_t t, uint32_t duration_ms, Fn pattern) {
  const uint32_t end = t + duration_ms;
  uint32_t rng = 12345;
  while (t < end) {
    c.sample(pattern(t), t);
    rng = rng * 1103515245u + 12345u;
    t += 2 + ((rng >> 16) % 3);  // 2-4 ms
  }
  return t;
}

static core::LedTuning defaultTuning() { return core::LedTuning{}; }

TEST(led_solid_light_is_on) {
  core::LedClassifier c;
  c.begin(defaultTuning(), 0);
  feed(c, 0, 3000, [](uint32_t) { return true; });
  CHECK(c.state() == core::PcState::On);
  CHECK_EQ(c.dutyPct(), 100);
}

TEST(led_dark_is_off) {
  core::LedClassifier c;
  c.begin(defaultTuning(), 0);
  feed(c, 0, 3000, [](uint32_t) { return false; });
  CHECK(c.state() == core::PcState::Off);
  CHECK_EQ(c.dutyPct(), 0);
}

TEST(led_fast_pwm_dimming_reads_as_on) {
  core::LedClassifier c;
  c.begin(defaultTuning(), 0);
  // 500 Hz PWM at 50% duty - the case that aliases with a fixed sampling period.
  feed(c, 0, 4000, [](uint32_t t) { return (t % 2) == 0; });
  CHECK(c.state() == core::PcState::On);
  CHECK(c.spreadPct() < defaultTuning().spread_pct);
}

TEST(led_dim_pwm_still_reads_as_on) {
  core::LedClassifier c;
  c.begin(defaultTuning(), 0);
  // 20% duty, 10 ms period - a dimmed but steadily driven LED.
  feed(c, 0, 4000, [](uint32_t t) { return (t % 10) < 2; });
  CHECK(c.state() == core::PcState::On);
}

TEST(led_slow_blink_is_sleep) {
  core::LedClassifier c;
  c.begin(defaultTuning(), 0);
  // 1 Hz blink, 500 ms on / 500 ms off - the classic sleep indicator.
  feed(c, 0, 8000, [](uint32_t t) { return (t % 1000) < 500; });
  CHECK(c.state() == core::PcState::Sleep);
  CHECK(c.spreadPct() >= defaultTuning().spread_pct);
}

TEST(led_breathing_is_sleep) {
  core::LedClassifier c;
  c.begin(defaultTuning(), 0);
  // 2 s triangular breathe rendered as PWM: duty ramps 0 -> 100 -> 0.
  feed(c, 0, 8000, [](uint32_t t) {
    const uint32_t phase = t % 2000;
    const uint32_t duty = phase < 1000 ? phase / 10 : (2000 - phase) / 10;  // 0..100
    return (t % 100) < duty;                                                // 10 ms PWM period
  });
  CHECK(c.state() == core::PcState::Sleep);
}

TEST(led_leaving_on_needs_confirmation) {
  core::LedClassifier c;
  c.begin(defaultTuning(), 0);
  uint32_t t = feed(c, 0, 3000, [](uint32_t) { return true; });
  CHECK(c.state() == core::PcState::On);
  t = feed(c, t, 1200, [](uint32_t) { return false; });  // the LED just went dark
  CHECK(c.state() == core::PcState::On);                 // not believed yet
  feed(c, t, 4000, [](uint32_t) { return false; });
  CHECK(c.state() == core::PcState::Off);                // believed now
}

TEST(led_a_brief_glitch_never_reads_as_off) {
  core::LedClassifier c;
  c.begin(defaultTuning(), 0);
  uint32_t t = feed(c, 0, 4000, [](uint32_t) { return true; });
  CHECK(c.state() == core::PcState::On);
  t = feed(c, t, 200, [](uint32_t) { return false; });    // a 200 ms dropout
  t = feed(c, t, 6000, [](uint32_t) { return true; });    // and back to normal
  CHECK(c.state() == core::PcState::On);                  // must never have flipped
}

TEST(led_entering_on_is_immediate) {
  core::LedClassifier c;
  c.begin(defaultTuning(), 0);
  uint32_t t = feed(c, 0, 3000, [](uint32_t) { return false; });
  CHECK(c.state() == core::PcState::Off);
  feed(c, t, 400, [](uint32_t) { return true; });  // 400 ms of solid light
  CHECK(c.state() == core::PcState::On);           // no waiting for a full window
}

TEST(led_state_is_unknown_until_a_window_completes) {
  core::LedClassifier c;
  c.begin(defaultTuning(), 0);
  CHECK(c.state() == core::PcState::Unknown);
  CHECK(!c.ready());
  uint32_t t = feed(c, 0, 100, [](uint32_t) { return false; });
  CHECK(!c.ready());
  feed(c, t, 2500, [](uint32_t) { return false; });
  CHECK(c.ready());
}

TEST(led_records_when_state_changed) {
  core::LedClassifier c;
  c.begin(defaultTuning(), 0);
  uint32_t t = feed(c, 0, 3000, [](uint32_t) { return false; });
  const uint32_t off_at = c.lastChangeMs();
  feed(c, t, 500, [](uint32_t) { return true; });
  CHECK(c.state() == core::PcState::On);
  CHECK(c.lastChangeMs() > off_at);
}

TEST(led_survives_a_stalled_sampler) {
  core::LedClassifier c;
  c.begin(defaultTuning(), 0);
  feed(c, 0, 3000, [](uint32_t) { return true; });
  CHECK(c.state() == core::PcState::On);
  c.sample(true, 60000);  // a 57 s gap, e.g. a long OTA upload
  CHECK(c.state() == core::PcState::On);
  feed(c, 60000, 5000, [](uint32_t) { return false; });
  CHECK(c.state() == core::PcState::Off);
}

TEST(led_every_state_has_a_name) {
  CHECK_STREQ(core::pcStateName(core::PcState::Unknown), "unknown");
  CHECK_STREQ(core::pcStateName(core::PcState::Off), "off");
  CHECK_STREQ(core::pcStateName(core::PcState::Sleep), "sleep");
  CHECK_STREQ(core::pcStateName(core::PcState::On), "on");
}

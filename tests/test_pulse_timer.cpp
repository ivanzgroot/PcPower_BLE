#include "test_main.h"
#include "core/pulse_timer.h"

TEST(pulse_runs_for_its_duration) {
  core::PulseTimer p;
  p.begin(15000);
  CHECK(p.start(1000, 300));
  CHECK(p.active());
  CHECK(!p.update(1200));
  CHECK(p.active());
  CHECK_EQ(p.remaining(1200), 100);
  CHECK(p.update(1300));   // the finishing edge
  CHECK(!p.active());
  CHECK(!p.update(1400));  // reported once only
  CHECK_EQ(p.lastFinishedAt(), 1300);
}

TEST(pulse_refuses_to_overlap) {
  core::PulseTimer p;
  p.begin(15000);
  CHECK(p.start(0, 300));
  CHECK(!p.start(100, 300));
  CHECK_EQ(p.remaining(100), 200);
}

TEST(pulse_ceiling_is_enforced) {
  core::PulseTimer p;
  p.begin(6000);
  CHECK(p.start(0, 999999));
  CHECK_EQ(p.remaining(0), 6000);
}

TEST(pulse_zero_duration_never_starts) {
  core::PulseTimer p;
  p.begin(6000);
  CHECK(!p.start(0, 0));
  CHECK(!p.active());
}

TEST(pulse_abort_releases_immediately) {
  core::PulseTimer p;
  p.begin(15000);
  p.start(0, 5000);
  p.abort();
  CHECK(!p.active());
  CHECK(!p.update(6000));
}

TEST(pulse_survives_millis_wraparound) {
  core::PulseTimer p;
  p.begin(15000);
  const uint32_t near_wrap = 0xFFFFFF00u;
  CHECK(p.start(near_wrap, 500));
  CHECK(!p.update(near_wrap + 100));
  CHECK(p.update(near_wrap + 500));  // wraps past zero
  CHECK(!p.active());
}

TEST(pulse_tracks_start_time) {
  core::PulseTimer p;
  p.begin(15000);
  p.start(4242, 100);
  CHECK_EQ(p.startedAt(), 4242);
}

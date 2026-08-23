#include "test_main.h"
#include "core/learner.h"

static void makeAddr(uint8_t out[6], uint8_t first, uint8_t last) {
  const uint8_t base[6] = {0x00, 0x1A, 0x7D, 0xDA, 0x71, 0x00};
  std::memcpy(out, base, 6);
  out[0] = first;
  out[5] = last;
}

TEST(learner_is_idle_until_started) {
  core::Learner l;
  CHECK(!l.active(0));
  uint8_t a[6];
  makeAddr(a, 0xC5, 1);
  l.feed(a, 1, -40, "pad");
  CHECK_EQ(l.count(), 0);  // feeding outside the window is ignored
}

TEST(learner_window_expires) {
  core::Learner l;
  l.start(1000, 5000);
  CHECK(l.active(1000));
  CHECK(l.active(5999));
  CHECK_EQ(l.remaining(3000), 3000);
  CHECK(!l.active(6000));
  CHECK_EQ(l.remaining(6000), 0);
}

TEST(learner_collects_and_sorts_by_signal) {
  core::Learner l;
  l.start(0, 5000);
  uint8_t weak[6], strong[6];
  makeAddr(weak, 0xC5, 1);
  makeAddr(strong, 0xC5, 2);
  l.feed(weak, 1, -80, "far");
  l.feed(strong, 1, -35, "near");
  CHECK_EQ(l.count(), 2);
  CHECK_EQ(l.at(0).best_rssi, -35);  // strongest first, even though it was fed last
  CHECK_STREQ(l.at(0).name, "near");
  CHECK_EQ(l.at(1).best_rssi, -80);
}

TEST(learner_merges_repeat_sightings) {
  core::Learner l;
  l.start(0, 5000);
  uint8_t a[6];
  makeAddr(a, 0xC5, 1);
  l.feed(a, 1, -70, "pad");
  l.feed(a, 1, -50, "pad");
  l.feed(a, 1, -60, "pad");
  CHECK_EQ(l.count(), 1);
  CHECK_EQ(l.at(0).best_rssi, -50);  // keeps the best, not the last
  CHECK_EQ(l.at(0).hits, 3);
}

TEST(learner_keeps_a_name_once_it_learns_one) {
  core::Learner l;
  l.start(0, 5000);
  uint8_t a[6];
  makeAddr(a, 0xC5, 1);
  l.feed(a, 1, -70, "");        // advertisement with no name
  l.feed(a, 1, -60, "8BitDo");  // scan response carries it
  l.feed(a, 1, -65, "");
  CHECK_STREQ(l.at(0).name, "8BitDo");
}

TEST(learner_records_rotating_addresses_but_skips_them) {
  core::Learner l;
  l.start(0, 5000);
  uint8_t rpa[6], stable[6];
  makeAddr(rpa, 0x45, 1);     // resolvable private
  makeAddr(stable, 0xC5, 2);  // random static
  l.feed(rpa, 1, -30, "rotating");
  l.feed(stable, 1, -60, "fixed");
  CHECK_EQ(l.count(), 2);
  CHECK_EQ(l.at(0).best_rssi, -30);  // the RPA is strongest and still listed
  CHECK(!core::isStable(l.at(0).kind));
  CHECK_EQ(l.bestStable(), 1);       // but the stable one is what gets suggested
}

TEST(learner_best_stable_is_minus_one_when_all_rotate) {
  core::Learner l;
  l.start(0, 5000);
  uint8_t rpa[6];
  makeAddr(rpa, 0x45, 1);
  l.feed(rpa, 1, -30, "rotating");
  CHECK_EQ(l.bestStable(), -1);
}

TEST(learner_cancel_stops_the_window) {
  core::Learner l;
  l.start(0, 5000);
  l.cancel();
  CHECK(!l.active(100));
}

TEST(learner_candidate_array_is_bounded) {
  core::Learner l;
  l.start(0, 5000);
  uint8_t a[6];
  for (int i = 0; i < 60; ++i) {
    makeAddr(a, 0xC5, (uint8_t)i);
    l.feed(a, 1, (int8_t)(-100 + i), "x");
  }
  CHECK(l.count() <= core::kMaxCandidates);
  CHECK_EQ(l.at(0).best_rssi, -41);  // the strongest survives the cap
}

TEST(learner_json_is_well_formed) {
  core::Learner l;
  l.start(0, 5000);
  uint8_t a[6];
  makeAddr(a, 0x45, 1);
  l.feed(a, 1, -30, "rotating");
  char buf[2048];
  const size_t n = l.toJson(buf, sizeof buf, 1000);
  CHECK(n > 0);
  CHECK(std::strstr(buf, "\"active\":true") != nullptr);
  CHECK(std::strstr(buf, "\"learnable\":false") != nullptr);
  CHECK(std::strstr(buf, "rotates its address") != nullptr);
}

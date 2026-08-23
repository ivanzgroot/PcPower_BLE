#include "test_main.h"
#include "core/ringlog.h"
#include <string>

TEST(ringlog_starts_empty) {
  core::RingLog log;
  CHECK_EQ(log.count(), 0);
  CHECK_EQ(log.total(), 0);
}

TEST(ringlog_keeps_order) {
  core::RingLog log;
  log.add(10, "first");
  log.add(20, "second");
  CHECK_EQ(log.count(), 2);
  CHECK_STREQ(log.line(0), "first");
  CHECK_STREQ(log.line(1), "second");
  CHECK_EQ(log.timestamp(1), 20);
  CHECK_EQ(log.total(), 2);
}

TEST(ringlog_wraps_and_drops_oldest) {
  core::RingLog log;
  char buf[16];
  for (int i = 0; i < 70; ++i) {
    std::snprintf(buf, sizeof buf, "line%d", i);
    log.add((uint32_t)i, buf);
  }
  CHECK_EQ(log.count(), core::kLogLines);
  CHECK_STREQ(log.line(0), "line6");                      // 70 - 64
  CHECK_STREQ(log.line(core::kLogLines - 1), "line69");
  CHECK_EQ(log.total(), 70);
}

TEST(ringlog_truncates_long_lines) {
  core::RingLog log;
  std::string huge(300, 'x');
  log.add(1, huge.c_str());
  CHECK_EQ(std::strlen(log.line(0)), core::kLogLineLen - 1);
}

TEST(ringlog_clear_resets_count_but_not_total) {
  core::RingLog log;
  log.add(1, "a");
  log.clear();
  CHECK_EQ(log.count(), 0);
  CHECK_EQ(log.total(), 1);
}

TEST(ringlog_out_of_range_index_is_safe) {
  core::RingLog log;
  log.add(1, "a");
  CHECK_STREQ(log.line(99), "");
  CHECK_EQ(log.timestamp(99), 0);
}

TEST(ringlog_handles_null_and_empty) {
  core::RingLog log;
  log.add(5, nullptr);
  log.add(6, "");
  CHECK_EQ(log.count(), 2);
  CHECK_STREQ(log.line(0), "");
  CHECK_STREQ(log.line(1), "");
}

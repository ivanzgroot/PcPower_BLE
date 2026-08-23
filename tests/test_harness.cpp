#include "test_main.h"

TEST(harness_runs_and_reports) {
  CHECK(1 + 1 == 2);
  CHECK_EQ(40 + 2, 42);
  CHECK_STREQ("pcpower", "pcpower");
  CHECK_NEAR(0.1 + 0.2, 0.3, 1e-9);
}

#include "test_main.h"

int main() {
  int before = 0;
  for (const auto& t : testRegistry()) {
    before = failCount();
    t.fn();
    std::printf("  %s %s\n", failCount() == before ? "ok  " : "FAIL", t.name);
  }
  std::printf("\n%zu tests, %d failures\n", testRegistry().size(), failCount());
  return failCount() == 0 ? 0 : 1;
}

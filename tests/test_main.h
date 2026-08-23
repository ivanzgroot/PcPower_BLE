// Minimal test harness for the PcPower_BLE core logic.
// Compiled natively by tools/run_tests.sh - no Arduino, no hardware.
#pragma once
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <cstdint>
#include <vector>

struct TestCase { const char* name; void (*fn)(); };
inline std::vector<TestCase>& testRegistry() { static std::vector<TestCase> v; return v; }
inline int& failCount() { static int c = 0; return c; }
struct TestRegistrar { TestRegistrar(const char* n, void (*f)()) { testRegistry().push_back({n, f}); } };

#define TEST(name)                                              \
  static void name();                                           \
  static TestRegistrar reg_##name(#name, name);                 \
  static void name()

#define CHECK(cond)                                                              \
  do {                                                                           \
    if (!(cond)) {                                                               \
      std::printf("    FAIL %s:%d  CHECK(%s)\n", __FILE__, __LINE__, #cond);     \
      failCount()++;                                                             \
    }                                                                            \
  } while (0)

#define CHECK_EQ(a, b)                                                           \
  do {                                                                           \
    long long _a = (long long)(a), _b = (long long)(b);                          \
    if (_a != _b) {                                                              \
      std::printf("    FAIL %s:%d  %s == %s  (got %lld, want %lld)\n",           \
                  __FILE__, __LINE__, #a, #b, _a, _b);                           \
      failCount()++;                                                             \
    }                                                                            \
  } while (0)

#define CHECK_STREQ(a, b)                                                        \
  do {                                                                           \
    const char* _a = (a); const char* _b = (b);                                  \
    if (std::strcmp(_a, _b) != 0) {                                              \
      std::printf("    FAIL %s:%d  %s == %s  (got \"%s\", want \"%s\")\n",       \
                  __FILE__, __LINE__, #a, #b, _a, _b);                           \
      failCount()++;                                                             \
    }                                                                            \
  } while (0)

#define CHECK_NEAR(a, b, eps)                                                    \
  do {                                                                           \
    double _a = (a), _b = (b);                                                   \
    if (std::fabs(_a - _b) > (eps)) {                                            \
      std::printf("    FAIL %s:%d  %s ~= %s  (got %f, want %f)\n",               \
                  __FILE__, __LINE__, #a, #b, _a, _b);                           \
      failCount()++;                                                             \
    }                                                                            \
  } while (0)

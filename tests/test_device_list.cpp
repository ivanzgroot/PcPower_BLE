#include "test_main.h"
#include "core/device_list.h"

static void makeAddr(uint8_t out[6], uint8_t last) {
  const uint8_t base[6] = {0xC5, 0x1A, 0x7D, 0xDA, 0x71, 0x00};
  std::memcpy(out, base, 6);
  out[5] = last;
}

TEST(devices_start_empty) {
  core::DeviceList list;
  CHECK_EQ(list.count(), 0);
  uint8_t a[6];
  makeAddr(a, 1);
  CHECK_EQ(list.find(a, 1), -1);
}

TEST(devices_add_then_find) {
  core::DeviceList list;
  uint8_t a[6];
  makeAddr(a, 1);
  CHECK_EQ(list.add(a, 1, "Controller"), 0);
  CHECK_EQ(list.count(), 1);
  CHECK_EQ(list.find(a, 1), 0);
  CHECK_STREQ(list.at(0).label, "Controller");
  CHECK(list.at(0).enabled);
}

TEST(devices_type_is_part_of_identity) {
  core::DeviceList list;
  uint8_t a[6];
  makeAddr(a, 1);
  list.add(a, 1, "random");
  CHECK_EQ(list.find(a, 0), -1);  // same bytes, different address type
}

TEST(devices_refuse_duplicates) {
  core::DeviceList list;
  uint8_t a[6];
  makeAddr(a, 1);
  CHECK_EQ(list.add(a, 1, "one"), 0);
  CHECK_EQ(list.add(a, 1, "again"), -2);
  CHECK_EQ(list.count(), 1);
}

TEST(devices_are_capped) {
  core::DeviceList list;
  uint8_t a[6];
  for (int i = 0; i < core::kMaxDevices; ++i) {
    makeAddr(a, (uint8_t)i);
    CHECK_EQ(list.add(a, 1, "x"), i);
  }
  makeAddr(a, 200);
  CHECK_EQ(list.add(a, 1, "one too many"), -1);
  CHECK_EQ(list.count(), core::kMaxDevices);
}

TEST(devices_remove_compacts) {
  core::DeviceList list;
  uint8_t a[6];
  for (int i = 0; i < 3; ++i) {
    makeAddr(a, (uint8_t)i);
    char label[8];
    std::snprintf(label, sizeof label, "d%d", i);
    list.add(a, 1, label);
  }
  CHECK(list.remove(0));
  CHECK_EQ(list.count(), 2);
  CHECK_STREQ(list.at(0).label, "d1");
  CHECK_STREQ(list.at(1).label, "d2");
  CHECK(!list.remove(9));
}

TEST(devices_label_is_truncated_not_overflowed) {
  core::DeviceList list;
  uint8_t a[6];
  makeAddr(a, 1);
  list.add(a, 1, "a-very-long-label-that-will-not-fit-in-the-field");
  CHECK_EQ(std::strlen(list.at(0).label), core::kLabelLen - 1);
  CHECK(list.setLabel(0, "short"));
  CHECK_STREQ(list.at(0).label, "short");
  CHECK(!list.setLabel(7, "nope"));
}

TEST(devices_enable_toggles) {
  core::DeviceList list;
  uint8_t a[6];
  makeAddr(a, 1);
  list.add(a, 1, "x");
  CHECK(list.setEnabled(0, false));
  CHECK(!list.at(0).enabled);
}

TEST(devices_mark_seen_reports_the_absence_gap) {
  core::DeviceList list;
  uint8_t a[6];
  makeAddr(a, 1);
  list.add(a, 1, "x");
  CHECK_EQ(list.markSeen(0, 1000, -50), UINT32_MAX);  // the first sighting
  CHECK_EQ(list.at(0).last_rssi, -50);
  CHECK_EQ(list.markSeen(0, 9000, -40), 8000);
  CHECK_EQ(list.markSeen(0, 9200, -41), 200);
  CHECK_EQ(list.at(0).last_seen_ms, 9200);
}

TEST(devices_json_is_well_formed) {
  core::DeviceList list;
  uint8_t a[6];
  makeAddr(a, 0x13);
  list.add(a, 1, "My \"pad\"");
  list.markSeen(0, 1000, -50);
  char buf[2048];
  const size_t n = list.toJson(buf, sizeof buf, 5000);
  CHECK(n > 0);
  CHECK(buf[0] == '[');
  CHECK(buf[n - 1] == ']');
  CHECK(std::strstr(buf, "C5:1A:7D:DA:71:13") != nullptr);
  CHECK(std::strstr(buf, "My \\\"pad\\\"") != nullptr);
  CHECK(std::strstr(buf, "\"enabled\":true") != nullptr);
}

TEST(devices_json_into_a_small_buffer_is_safe) {
  core::DeviceList list;
  uint8_t a[6];
  makeAddr(a, 1);
  list.add(a, 1, "x");
  char small[24];
  const size_t n = list.toJson(small, sizeof small, 0);
  CHECK(n < sizeof small);
  CHECK_EQ(small[sizeof small - 1], 0);
}

TEST(devices_clear_empties) {
  core::DeviceList list;
  uint8_t a[6];
  makeAddr(a, 1);
  list.add(a, 1, "x");
  list.clear();
  CHECK_EQ(list.count(), 0);
}

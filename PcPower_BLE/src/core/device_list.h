// The known-device list: which addresses are allowed to wake the PC, and when each was last seen.
#pragma once
#include <cstddef>
#include <cstdint>

#include "core/ble_addr.h"

namespace core {

static constexpr uint8_t kMaxDevices = 32;
static constexpr size_t kLabelLen = 24;

struct Device {
  uint8_t addr[6] = {0};  // MSB first, display order
  uint8_t addr_type = 0;
  bool enabled = true;
  char label[kLabelLen] = {0};
  uint32_t last_seen_ms = 0;  // 0 means not seen since boot
  uint32_t prev_seen_ms = 0;
  int8_t last_rssi = 0;
  uint32_t trigger_count = 0;
};

class DeviceList {
 public:
  uint8_t count() const { return count_; }
  int find(const uint8_t addr[6], uint8_t addr_type) const;  // -1 when absent
  int add(const uint8_t addr[6], uint8_t addr_type, const char* label);  // -1 full, -2 duplicate
  bool remove(uint8_t index);
  bool setLabel(uint8_t index, const char* label);
  bool setEnabled(uint8_t index, bool enabled);

  Device& at(uint8_t index) { return devices_[index]; }
  const Device& at(uint8_t index) const { return devices_[index]; }

  // Records a sighting and returns the gap since the previous one (UINT32_MAX on the first).
  uint32_t markSeen(uint8_t index, uint32_t now_ms, int8_t rssi);

  void clear() { count_ = 0; }
  size_t toJson(char* buf, size_t len, uint32_t now_ms) const;

 private:
  Device devices_[kMaxDevices];
  uint8_t count_ = 0;
};

}  // namespace core

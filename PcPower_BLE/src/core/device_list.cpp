#include "device_list.h"

#include <cstdio>
#include <cstring>

#include "json_out.h"

namespace core {

int DeviceList::find(const uint8_t addr[6], uint8_t addr_type) const {
  for (uint8_t i = 0; i < count_; ++i) {
    if (devices_[i].addr_type == addr_type && addrEqual(devices_[i].addr, addr)) return i;
  }
  return -1;
}

int DeviceList::add(const uint8_t addr[6], uint8_t addr_type, const char* label) {
  if (find(addr, addr_type) >= 0) return -2;
  if (count_ >= kMaxDevices) return -1;
  Device& d = devices_[count_];
  d = Device{};
  std::memcpy(d.addr, addr, 6);
  d.addr_type = addr_type;
  d.enabled = true;
  std::snprintf(d.label, kLabelLen, "%s", label ? label : "");
  return count_++;
}

bool DeviceList::remove(uint8_t index) {
  if (index >= count_) return false;
  for (uint8_t i = index; i + 1 < count_; ++i) devices_[i] = devices_[i + 1];
  --count_;
  return true;
}

bool DeviceList::setLabel(uint8_t index, const char* label) {
  if (index >= count_) return false;
  std::snprintf(devices_[index].label, kLabelLen, "%s", label ? label : "");
  return true;
}

bool DeviceList::setEnabled(uint8_t index, bool enabled) {
  if (index >= count_) return false;
  devices_[index].enabled = enabled;
  return true;
}

uint32_t DeviceList::markSeen(uint8_t index, uint32_t now_ms, int8_t rssi) {
  if (index >= count_) return UINT32_MAX;
  Device& d = devices_[index];
  const uint32_t previous = d.last_seen_ms;
  d.prev_seen_ms = previous;
  d.last_seen_ms = now_ms;
  d.last_rssi = rssi;
  return previous == 0 ? UINT32_MAX : now_ms - previous;
}

size_t DeviceList::toJson(char* buf, size_t len, uint32_t now_ms) const {
  size_t pos = 0;
  jsonAppend(buf, len, &pos, "[");
  for (uint8_t i = 0; i < count_; ++i) {
    const Device& d = devices_[i];
    char addr[18];
    formatAddress(d.addr, addr, sizeof addr);
    const uint32_t since = d.last_seen_ms == 0 ? UINT32_MAX : now_ms - d.last_seen_ms;
    jsonAppend(buf, len, &pos, "%s{\"index\":%u,\"addr\":\"%s\",\"type\":%u,\"label\":\"", i ? "," : "",
               (unsigned)i, addr, (unsigned)d.addr_type);
    jsonAppendEscaped(buf, len, &pos, d.label);
    jsonAppend(buf, len, &pos,
               "\",\"enabled\":%s,\"seen\":%s,\"last_seen_ms\":%u,\"rssi\":%d,\"triggers\":%u,"
               "\"kind\":\"%s\"}",
               d.enabled ? "true" : "false", d.last_seen_ms ? "true" : "false", (unsigned)since,
               (int)d.last_rssi, (unsigned)d.trigger_count,
               addrKindName(classifyAddress(d.addr, d.addr_type)));
  }
  jsonAppend(buf, len, &pos, "]");
  return pos;
}

}  // namespace core

#include "device_store.h"

#include <Preferences.h>

#include "app.h"

static const char* kNamespace = "devs";
static constexpr uint8_t kBlobVersion = 1;

// Only what must survive a reboot. Sighting timestamps are per-boot and deliberately absent.
struct StoredDevice {
  uint8_t version;
  uint8_t addr[6];
  uint8_t addr_type;
  uint8_t enabled;
  char label[core::kLabelLen];
  uint32_t trigger_count;
};

namespace DeviceStore {

void load(core::DeviceList& list) {
  list.clear();

  Preferences prefs;
  if (!prefs.begin(kNamespace, /*readOnly=*/true)) return;

  const int count = prefs.getUChar("n", 0);
  for (int i = 0; i < count && i < core::kMaxDevices; ++i) {
    char key[8];
    snprintf(key, sizeof key, "d%d", i);
    StoredDevice stored{};
    const size_t n = prefs.getBytes(key, &stored, sizeof stored);
    if (n != sizeof stored || stored.version != kBlobVersion) {
      appLogf("devices: entry %d unreadable, skipped", i);
      continue;
    }
    stored.label[core::kLabelLen - 1] = '\0';
    const int index = list.add(stored.addr, stored.addr_type, stored.label);
    if (index >= 0) {
      list.at((uint8_t)index).enabled = stored.enabled != 0;
      list.at((uint8_t)index).trigger_count = stored.trigger_count;
    }
  }
  prefs.end();
}

void save(const core::DeviceList& list) {
  Preferences prefs;
  if (!prefs.begin(kNamespace, /*readOnly=*/false)) {
    appLog("devices: could not open NVS for writing");
    return;
  }
  prefs.clear();
  prefs.putUChar("n", list.count());
  for (uint8_t i = 0; i < list.count(); ++i) {
    const core::Device& d = list.at(i);
    StoredDevice stored{};
    stored.version = kBlobVersion;
    memcpy(stored.addr, d.addr, 6);
    stored.addr_type = d.addr_type;
    stored.enabled = d.enabled ? 1 : 0;
    snprintf(stored.label, sizeof stored.label, "%s", d.label);
    stored.trigger_count = d.trigger_count;
    char key[8];
    snprintf(key, sizeof key, "d%d", i);
    prefs.putBytes(key, &stored, sizeof stored);
  }
  prefs.end();
}

}  // namespace DeviceStore

#include "diagnostics.h"

#include <Preferences.h>
#include <esp_heap_caps.h>

#include "app.h"
#include "core/json_out.h"

static const char* kNamespace = "diag";
static constexpr uint32_t kFlushIntervalMs = 5 * 60 * 1000;  // cheap at this rate: NVS is wear-leveled

static uint32_t s_ble_soft_restarts = 0;
static uint32_t s_ble_hard_restarts = 0;
static uint32_t s_ap_start_failures = 0;
static uint32_t s_last_flush_ms = 0;

static void writeSnapshot() {
  Preferences prefs;
  if (!prefs.begin(kNamespace, false)) return;
  prefs.putUInt("uptime_s", millis() / 1000);
  prefs.putUInt("min_heap", ESP.getMinFreeHeap());
  prefs.putUInt("max_block", (uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
  prefs.putUInt("ble_soft", s_ble_soft_restarts);
  prefs.putUInt("ble_hard", s_ble_hard_restarts);
  prefs.putUInt("ap_fail", s_ap_start_failures);
  prefs.end();
}

namespace Diag {

void begin() {
  Preferences prefs;
  if (prefs.begin(kNamespace, true)) {
    if (prefs.isKey("uptime_s")) {
      appLogf("diag: previous session ran %us, min heap %uB, largest block %uB, "
              "ble restarts %u soft / %u hard, %u AP start failures",
              (unsigned)prefs.getUInt("uptime_s", 0), (unsigned)prefs.getUInt("min_heap", 0),
              (unsigned)prefs.getUInt("max_block", 0), (unsigned)prefs.getUInt("ble_soft", 0),
              (unsigned)prefs.getUInt("ble_hard", 0), (unsigned)prefs.getUInt("ap_fail", 0));
    }
    prefs.end();
  }
  s_last_flush_ms = millis();
}

void tick() {
  const uint32_t now = millis();
  if (now - s_last_flush_ms >= kFlushIntervalMs) {
    s_last_flush_ms = now;
    writeSnapshot();
  }
}

void noteBleSoftRestart() { ++s_ble_soft_restarts; }
void noteBleHardRestart() { ++s_ble_hard_restarts; }
void noteApStartFailure() { ++s_ap_start_failures; }

uint32_t minFreeHeapBytes() { return ESP.getMinFreeHeap(); }
size_t largestFreeBlockBytes() { return heap_caps_get_largest_free_block(MALLOC_CAP_8BIT); }

size_t toJson(char* buf, size_t len) {
  size_t pos = 0;
  core::jsonAppend(buf, len, &pos,
                   "{\"min_heap\":%u,\"largest_block\":%u,\"ble_soft_restarts\":%u,"
                   "\"ble_hard_restarts\":%u,\"ap_start_failures\":%u}",
                   (unsigned)minFreeHeapBytes(), (unsigned)largestFreeBlockBytes(),
                   (unsigned)s_ble_soft_restarts, (unsigned)s_ble_hard_restarts,
                   (unsigned)s_ap_start_failures);
  return pos;
}

}  // namespace Diag

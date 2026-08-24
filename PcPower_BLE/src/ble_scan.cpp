#include "ble_scan.h"

#include <NimBLEDevice.h>

#include "app.h"
#include "core/radio_policy.h"
#include "device_store.h"
#include "pc_sense.h"
#include "power_out.h"

static core::Learner s_learner;
static NimBLEScan* s_scan = nullptr;

static volatile uint32_t s_adverts = 0;
static volatile uint32_t s_last_advert_ms = 0;
static volatile bool s_paused = false;
static volatile bool s_external_inhibit = false;  // OTA
static volatile bool s_learn_inhibit = false;    // a candidate must not press the button
static volatile bool s_started = false;

static volatile core::TriggerReason s_last_reason = core::TriggerReason::UnknownDevice;
static volatile uint32_t s_last_reason_ms = 0;
static volatile bool s_have_reason = false;

// Set by the callback, consumed by tick(). Formatting a log line is far too slow to do on the
// path between hearing a controller and pressing the button.
static volatile int s_pending_fire = -1;
static volatile bool s_pending_devices_dirty = false;

// Blocked decisions repeat every advertisement, which would fill the 64-line log in seconds.
static core::TriggerReason s_logged_reason = core::TriggerReason::Fire;
static uint32_t s_logged_reason_ms = 0;

static uint8_t s_restart_count = 0;
static uint32_t s_last_watchdog_ms = 0;

static bool s_sharing_antenna = false;

static void applyScanSettings(const core::Settings& s) {
  if (!s_scan) return;
  const int32_t interval = s.num(core::S_SCAN_INTVL_MS);
  // Only yield duty cycle to WiFi while WiFi is actually powered. In exclusive mode the scanner
  // has the antenna to itself and there is nothing to be polite to.
  const uint16_t window = core::effectiveScanWindowMs(interval, s.num(core::S_SCAN_WINDOW_MS),
                                                      s_sharing_antenna);
  s_scan->setActiveScan(s.flag(core::S_SCAN_ACTIVE));
  s_scan->setInterval((uint16_t)interval);  // milliseconds in NimBLE 2.x
  s_scan->setWindow(window);
  s_scan->setDuplicateFilter(0);  // repeats ARE the signal - never filter them
  s_scan->setMaxResults(0);       // a live listener, not a result collector
}

static void startScan() {
  if (!s_scan) return;
  s_scan->start(0, /*isContinue=*/false, /*restart=*/true);  // 0 = scan forever
  s_started = true;
}

class ScanCallbacks : public NimBLEScanCallbacks {
 public:
  void onDiscovered(const NimBLEAdvertisedDevice* dev) override {
    // NimBLE holds addresses least-significant byte first; ours are display order.
    uint8_t addr[6];
    const uint8_t* raw = dev->getAddress().getVal();
    for (int i = 0; i < 6; ++i) addr[i] = raw[5 - i];
    const uint8_t type = dev->getAddress().getType();
    const int8_t rssi = (int8_t)dev->getRSSI();
    const uint32_t now = millis();

    ++s_adverts;
    s_last_advert_ms = now;

    if (s_learner.active(now)) {
      s_learner.feed(addr, type, rssi, "");
      return;
    }

    if (!g_devices_mutex) return;
    if (xSemaphoreTake(g_devices_mutex, pdMS_TO_TICKS(5)) != pdTRUE) return;

    const int index = g_devices.find(addr, type);
    if (index < 0) {
      xSemaphoreGive(g_devices_mutex);
      return;  // not ours: the cheapest possible exit
    }

    const uint32_t absent = g_devices.markSeen((uint8_t)index, now, rssi);

    core::TriggerInputs in;
    in.device_known = true;
    in.device_enabled = g_devices.at((uint8_t)index).enabled;
    in.pc_state = PcSense::state();
    in.ms_since_pc_off_edge = PcSense::msSinceOffEdge();
    in.ms_since_last_pulse = PowerOut::msSinceLastPulse();
    in.device_absent_ms = absent;
    in.rssi = rssi;
    in.inhibited = s_external_inhibit || s_learn_inhibit;

    const core::TriggerReason reason = core::evaluate(in, triggerConfigFrom(g_settings));
    s_last_reason = reason;
    s_last_reason_ms = now;
    s_have_reason = true;

    if (core::shouldFire(reason)) {
      PowerOut::pulseShort();  // asserts the pin and returns
      g_devices.at((uint8_t)index).trigger_count++;
      s_pending_fire = index;
      s_pending_devices_dirty = true;
    }
    xSemaphoreGive(g_devices_mutex);
  }

  // Fires once the scan response has arrived, so this is where a device name is available.
  void onResult(const NimBLEAdvertisedDevice* dev) override {
    const uint32_t now = millis();
    if (!s_learner.active(now)) return;
    uint8_t addr[6];
    const uint8_t* raw = dev->getAddress().getVal();
    for (int i = 0; i < 6; ++i) addr[i] = raw[5 - i];
    s_learner.feed(addr, dev->getAddress().getType(), (int8_t)dev->getRSSI(),
                   dev->getName().c_str());
  }

  void onScanEnd(const NimBLEScanResults&, int reason) override {
    // A scan that quietly ends is the one failure that would silently break the whole product.
    if (!s_paused) {
      s_started = false;
      startScan();
      if (reason != 0) appLogf("ble: scan ended (%d), restarted", reason);
    }
  }
};

static ScanCallbacks s_callbacks;

namespace BleScan {

void begin(const core::Settings& s) {
  NimBLEDevice::init("");
  NimBLEDevice::setPower(9);
  s_scan = NimBLEDevice::getScan();
  s_scan->setScanCallbacks(&s_callbacks, /*wantDuplicates=*/true);
  applyScanSettings(s);
  startScan();
  s_last_watchdog_ms = millis();
  appLogf("ble: scanning %d/%d ms%s%s", (int)s.num(core::S_SCAN_INTVL_MS),
          (int)core::effectiveScanWindowMs(s.num(core::S_SCAN_INTVL_MS),
                                           s.num(core::S_SCAN_WINDOW_MS), s_sharing_antenna),
          s.flag(core::S_SCAN_ACTIVE) ? " active" : "",
          s_sharing_antenna ? ", sharing with WiFi" : ", whole antenna");
}

void reconfigure(const core::Settings& s) {
  if (!s_scan) return;
  const bool was_scanning = s_started;
  if (was_scanning) s_scan->stop();
  applyScanSettings(s);
  if (was_scanning && !s_paused) startScan();
}

void setPaused(bool paused) {
  if (paused == s_paused) return;
  s_paused = paused;
  if (!s_scan) return;
  if (paused) {
    s_scan->stop();
    s_started = false;
    appLog("ble: scan paused, PC is running");
  } else {
    startScan();
    appLog("ble: scan resumed");
  }
}

bool paused() { return s_paused; }

void setSharingAntenna(bool sharing) {
  if (sharing == s_sharing_antenna) return;
  s_sharing_antenna = sharing;
  appLogf("ble: %s", sharing ? "sharing the antenna with WiFi, easing the duty cycle"
                             : "whole antenna, scanning at the configured duty cycle");
  reconfigure(g_settings);
}
bool scanning() { return s_scan && s_scan->isScanning(); }

void setInhibited(bool inhibit) { s_external_inhibit = inhibit; }
bool inhibited() { return s_external_inhibit || s_learn_inhibit; }

void startLearning(uint32_t duration_ms) {
  s_learner.start(millis(), duration_ms);
  s_learn_inhibit = true;  // a candidate must not press the button while it is held close
  s_paused = false;

  // Restart the scan outright rather than trusting the paused flag. The flag says what we asked
  // for, not what the radio is doing, and a WiFi power cycle underneath can leave the scan
  // stopped with the flag still saying otherwise - which shows up as a learn that finds nothing.
  if (s_scan) {
    s_scan->stop();
    startScan();
  }
  appLogf("learn: listening for %u ms, scanner restarted", (unsigned)duration_ms);
}

void cancelLearning() {
  s_learner.cancel();
  s_learn_inhibit = false;
  appLog("learn: cancelled");
}

bool learning() { return s_learner.active(millis()); }
core::Learner& learner() { return s_learner; }

uint32_t advertsSeen() { return s_adverts; }

uint32_t msSinceLastAdvert() {
  if (s_last_advert_ms == 0) return UINT32_MAX;
  return millis() - s_last_advert_ms;
}

core::TriggerReason lastReason() { return s_last_reason; }

uint32_t msSinceLastReason() {
  if (!s_have_reason) return UINT32_MAX;
  return millis() - s_last_reason_ms;
}

void tick() {
  const uint32_t now = millis();

  if (s_pending_fire >= 0) {
    const int index = s_pending_fire;
    s_pending_fire = -1;
    if (index < g_devices.count()) {
      appLogf("WAKE: %s pressed the power button", g_devices.at((uint8_t)index).label);
    }
    s_logged_reason = core::TriggerReason::Fire;
  } else if (s_have_reason && s_last_reason != core::TriggerReason::Fire) {
    // Log a repeated block at most once every 5 seconds.
    if (s_last_reason != s_logged_reason || (uint32_t)(now - s_logged_reason_ms) > 5000) {
      s_logged_reason = s_last_reason;
      s_logged_reason_ms = now;
      appLogf("blocked: %s", core::triggerReasonText(s_last_reason));
    }
  }

  if (s_pending_devices_dirty) {
    s_pending_devices_dirty = false;
    DeviceStore::save(g_devices);  // persist the trigger counter, off the fast path
  }

  // Learning windows close themselves; release the inhibit when one does, whether or not it
  // heard anything. This must not touch the OTA inhibit, which is released by the web layer.
  if (s_learn_inhibit && !s_learner.active(now)) s_learn_inhibit = false;

  // Scan watchdog. Hearing nothing at all for 30 s means the radio, not the neighbourhood.
  if (!s_paused && s_started && (uint32_t)(now - s_last_watchdog_ms) > 30000) {
    s_last_watchdog_ms = now;
    const uint32_t quiet = msSinceLastAdvert();
    if (quiet > 30000) {
      ++s_restart_count;
      if (s_restart_count >= 3) {
        appLog("ble: no adverts after 3 restarts, reinitialising the stack");
        s_restart_count = 0;
        NimBLEDevice::deinit(true);
        s_scan = nullptr;
        begin(g_settings);
      } else {
        appLogf("ble: %u s without an advert, restarting the scan", (unsigned)(quiet / 1000));
        startScan();
      }
    } else {
      s_restart_count = 0;
    }
  }
}

}  // namespace BleScan

#include "radio.h"

#include "app.h"
#include "ble_scan.h"
#include "core/json_out.h"
#include "net.h"
#include "pc_sense.h"
#include "web_server.h"

// How long a change in PC state must hold before the radios are switched. Bringing WiFi up and
// down is expensive, so a flickering sense reading must not be able to make the board do it
// repeatedly. The classifier already debounces, so this is a second line of defence.
static constexpr uint32_t kDwellMs = 5000;

static core::RadioArbiter s_arbiter;
static core::RadioPlan s_plan;
static bool s_applied = false;
static bool s_web_started = false;

static core::RadioConfig configFrom(const core::Settings& s) {
  core::RadioConfig cfg;
  cfg.exclusive = s.flag(core::S_RADIO_EXCLUSIVE);
  cfg.pause_scan_when_pc_on = s.flag(core::S_PAUSE_WHEN_ON);
  cfg.sense_forced_off = s.num(core::S_SENSE_MODE) == (int32_t)core::SenseMode::ForceOff;
  cfg.dwell_ms = kDwellMs;
  return cfg;
}

namespace Radio {

void begin(const core::Settings& s) {
  s_arbiter.begin(millis());
  s_applied = false;
  if (s.flag(core::S_RADIO_EXCLUSIVE) &&
      s.num(core::S_SENSE_MODE) == (int32_t)core::SenseMode::ForceOff) {
    appLog("radio: exclusive mode ignored, the sense is forced off so the PC never reads as on");
  }
}

void tick() {
  core::RadioInputs in;
  in.pc_state = PcSense::state();
  in.ota_in_progress = Web::otaInProgress();
  in.learning = BleScan::learning();

  const core::RadioPlan plan = s_arbiter.update(in, configFrom(g_settings), millis());

  if (!s_applied || plan.wifi_enabled != s_plan.wifi_enabled) {
    if (plan.wifi_enabled) {
      Net::resume();
      // The listening socket is opened the first time there is an interface to open it on.
      if (!s_web_started) {
        Web::begin();
        s_web_started = true;
      }
    } else {
      Net::shutdown();
    }
  }

  if (!s_applied || plan.ble_scanning != s_plan.ble_scanning) {
    BleScan::setPaused(!plan.ble_scanning);
  }

  // The scanner only has to ease off when WiFi is genuinely powered alongside it, which in
  // exclusive mode happens only during a learn started from the web interface.
  BleScan::setSharingAntenna(plan.wifi_enabled && plan.ble_scanning);

  s_plan = plan;
  s_applied = true;
}

core::RadioOwner owner() { return s_plan.owner; }
bool wifiEnabled() { return s_plan.wifi_enabled; }
bool bleScanning() { return s_plan.ble_scanning; }

bool exclusive() {
  const core::RadioConfig cfg = configFrom(g_settings);
  return cfg.exclusive && !cfg.sense_forced_off;
}

size_t toJson(char* buf, size_t len) {
  size_t pos = 0;
  core::jsonAppend(buf, len, &pos,
                   "{\"mode\":\"%s\",\"owner\":\"%s\",\"wifi\":%s,\"scanning\":%s,"
                   "\"settled\":%s}",
                   exclusive() ? "exclusive" : "shared", core::radioOwnerName(s_plan.owner),
                   s_plan.wifi_enabled ? "true" : "false",
                   s_plan.ble_scanning ? "true" : "false",
                   s_arbiter.settled() ? "true" : "false");
  return pos;
}

}  // namespace Radio

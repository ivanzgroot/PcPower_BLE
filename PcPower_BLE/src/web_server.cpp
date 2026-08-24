#include "web_server.h"

#include <Update.h>
#include <WebServer.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>

#include "app.h"
#include "ble_scan.h"
#include "core/json_out.h"
#include "device_store.h"
#include "net.h"
#include "pc_sense.h"
#include "power_out.h"
#include "radio.h"
#include "settings_store.h"
#include "status_led.h"
#include "web_ui.h"

static WebServer s_server(80);

// One shared buffer. Building responses with String would fragment the heap that BLE needs.
static char s_buf[8192];

static bool s_ota_running = false;
static uint8_t s_ota_percent = 0;
static size_t s_ota_written = 0;
static size_t s_ota_total = 0;
static uint32_t s_reboot_at = 0;
static bool s_ota_rejected = false;
static bool s_ota_failed = false;
static char s_ota_error[192] = {0};

// The slot a new firmware image would be written to, or null when this board's partition
// table has no second app slot and therefore cannot be updated over the air at all.
static const esp_partition_t* otaTargetPartition() {
  const esp_partition_t* next = esp_ota_get_next_update_partition(nullptr);
  if (!next || next == esp_ota_get_running_partition()) return nullptr;
  return next;
}

// Puts everything back the way it was before the upload started. Missing this is how a failed
// update leaves the board alive but deaf: scanning paused and triggering inhibited, with no
// sign of it except that the PC stops waking up.
static void otaRelease() {
  s_ota_running = false;
  s_ota_percent = 0;
  BleScan::setInhibited(false);
  BleScan::setPaused(false);  // loop() re-pauses on its own if the PC is running
}

// --- request guards --------------------------------------------------------
//
// There is no authentication here, by design - this board lives on a trusted LAN. That makes
// two cheap checks worth having, because a browser on that LAN can be pointed at us by any web
// page the user happens to visit:
//
//   Host   - blocks DNS rebinding. An attacker's domain resolving to our IP arrives with their
//            hostname in the Host header, not ours, so we refuse it.
//   Origin - blocks CSRF. Browsers attach Origin to cross-origin POSTs; our own page sends its
//            own origin. Requests with no Origin at all (curl, the serial console's sibling
//            tools) are allowed through, so scripting the API stays as easy as it was.

static bool hostAllowed() {
  String host = s_server.hostHeader();
  if (host.length() == 0) return false;
  const int colon = host.indexOf(':');
  if (colon >= 0) host = host.substring(0, colon);

  if (host == Net::ip()) return true;
  if (host == "localhost" || host == "192.168.4.1") return true;

  const String name(Net::hostname());
  return host.equalsIgnoreCase(name) || host.equalsIgnoreCase(name + ".local");
}

static bool originAllowed() {
  const String origin = s_server.header("Origin");
  if (origin.length() == 0) return true;  // not a browser, or a same-origin GET
  const String expected = String("http://") + s_server.hostHeader();
  return origin.equalsIgnoreCase(expected);
}

static bool requestAllowed() {
  if (!hostAllowed()) {
    s_server.send(421, "text/plain", "wrong host - reach this board by its own name or IP");
    return false;
  }
  if (!originAllowed()) {
    s_server.send(403, "text/plain", "cross-origin requests are refused");
    return false;
  }
  return true;
}

// --- helpers ---------------------------------------------------------------

static void sendJson(int code, const char* json) {
  s_server.sendHeader("Cache-Control", "no-store");
  s_server.send(code, "application/json", json);
}

static void sendOk() { sendJson(200, "{\"ok\":true}"); }

static void sendError(int code, const char* message) {
  size_t pos = 0;
  core::jsonAppend(s_buf, sizeof s_buf, &pos, "{\"ok\":false,\"error\":\"");
  core::jsonAppendEscaped(s_buf, sizeof s_buf, &pos, message);
  core::jsonAppend(s_buf, sizeof s_buf, &pos, "\"}");
  sendJson(code, s_buf);
}

static bool argInt(const char* name, long* out) {
  if (!s_server.hasArg(name)) return false;
  const String value = s_server.arg(name);
  char* end = nullptr;
  const long parsed = strtol(value.c_str(), &end, 10);
  if (end == value.c_str() || *end != '\0') return false;
  *out = parsed;
  return true;
}

// Applies a settings change everywhere it matters, without a reboot.
static void applySettingsToHardware() {
  g_settings.applyCoupling(Net::staConnected());
  PowerOut::reconfigure(g_settings);
  PcSense::reconfigure(g_settings);
  StatusLed::reconfigure(g_settings);
  BleScan::reconfigure(g_settings);
}

// --- handlers --------------------------------------------------------------

static void handleRoot() {
  s_server.sendHeader("Cache-Control", "public, max-age=600");
  s_server.send_P(200, "text/html", WEB_INDEX_HTML);
}

static void handleStatus() {
  size_t pos = 0;
  const core::PcState pc = PcSense::state();
  core::jsonAppend(s_buf, sizeof s_buf, &pos,
                   "{\"pc\":{\"state\":\"%s\",\"raw\":\"%s\",\"duty\":%u,\"spread\":%u,"
                   "\"ready\":%s,\"lit\":%s,\"ms_since_off\":%u,\"mode\":\"%s\"},",
                   core::pcStateName(pc), core::pcStateName(PcSense::rawState()),
                   (unsigned)PcSense::dutyPct(), (unsigned)PcSense::spreadPct(),
                   PcSense::ready() ? "true" : "false", PcSense::litNow() ? "true" : "false",
                   (unsigned)PcSense::msSinceOffEdge(),
                   g_settings.num(core::S_SENSE_MODE) == 0 ? "auto" : "force_off");

  core::jsonAppend(s_buf, sizeof s_buf, &pos, "\"net\":");
  pos += Net::statusToJson(s_buf + pos, sizeof s_buf - pos);

  core::jsonAppend(s_buf, sizeof s_buf, &pos, ",\"radio\":");
  pos += Radio::toJson(s_buf + pos, sizeof s_buf - pos);

  const core::TriggerReason reason = BleScan::lastReason();
  core::jsonAppend(s_buf, sizeof s_buf, &pos,
                   ",\"scan\":{\"scanning\":%s,\"paused\":%s,\"inhibited\":%s,\"adverts\":%u,"
                   "\"last_advert_ms\":%u,\"last_reason\":\"%s\",\"last_reason_text\":\"%s\","
                   "\"last_reason_ms\":%u},",
                   BleScan::scanning() ? "true" : "false", BleScan::paused() ? "true" : "false",
                   BleScan::inhibited() ? "true" : "false", (unsigned)BleScan::advertsSeen(),
                   (unsigned)BleScan::msSinceLastAdvert(), core::triggerReasonName(reason),
                   core::triggerReasonText(reason), (unsigned)BleScan::msSinceLastReason());

  core::jsonAppend(s_buf, sizeof s_buf, &pos,
                   "\"pulse\":{\"count\":%u,\"ms_since_last\":%u,\"active\":%s},",
                   (unsigned)PowerOut::pulseCount(), (unsigned)PowerOut::msSinceLastPulse(),
                   PowerOut::active() ? "true" : "false");

  core::jsonAppend(s_buf, sizeof s_buf, &pos, "\"learn\":{\"active\":%s,\"remaining_ms\":%u},",
                   BleScan::learning() ? "true" : "false",
                   (unsigned)BleScan::learner().remaining(millis()));

  core::jsonAppend(s_buf, sizeof s_buf, &pos,
                   "\"ota\":{\"running\":%s,\"percent\":%u,\"capable\":%s,\"partition\":\"%s\","
                   "\"slot_bytes\":%u},\"uptime_ms\":%u,\"heap\":%u,"
                   "\"version\":\"%s\",\"devices\":",
                   s_ota_running ? "true" : "false", (unsigned)s_ota_percent,
                   otaTargetPartition() ? "true" : "false",
                   esp_ota_get_running_partition() ? esp_ota_get_running_partition()->label : "?",
                   (unsigned)(otaTargetPartition() ? otaTargetPartition()->size : 0),
                   (unsigned)millis(), (unsigned)ESP.getFreeHeap(), kVersion);
  pos += g_devices.toJson(s_buf + pos, sizeof s_buf - pos, millis());
  core::jsonAppend(s_buf, sizeof s_buf, &pos, "}");
  sendJson(200, s_buf);
}

static void handleConfigGet() {
  g_settings.toJson(s_buf, sizeof s_buf);
  sendJson(200, s_buf);
}

static void handleConfigSchema() {
  core::Settings::schemaToJson(s_buf, sizeof s_buf);
  sendJson(200, s_buf);
}

static void handleConfigPost() {
  char err[96] = {0};
  char clamped[256] = {0};
  size_t clamped_pos = 0;
  int applied = 0;

  for (int i = 0; i < s_server.args(); ++i) {
    const String key = s_server.argName(i);
    if (key == "plain") continue;  // the raw body, not a field
    bool was_clamped = false;
    if (!g_settings.setByKey(key.c_str(), s_server.arg(i).c_str(), err, sizeof err,
                             &was_clamped)) {
      sendError(400, err);
      return;
    }
    ++applied;
    if (was_clamped) {
      core::jsonAppend(clamped, sizeof clamped, &clamped_pos, "%s\"", clamped_pos ? "," : "");
      core::jsonAppendEscaped(clamped, sizeof clamped, &clamped_pos, key.c_str());
      core::jsonAppend(clamped, sizeof clamped, &clamped_pos, "\"");
    }
  }

  applySettingsToHardware();
  SettingsStore::save(g_settings);
  appLogf("config: %d setting%s changed", applied, applied == 1 ? "" : "s");

  size_t pos = 0;
  core::jsonAppend(s_buf, sizeof s_buf, &pos, "{\"ok\":true,\"applied\":%d,\"clamped\":[%s]}",
                   applied, clamped);
  sendJson(200, s_buf);
}

static void handleConfigDefaults() {
  SettingsStore::restoreDefaults(g_settings);
  applySettingsToHardware();
  SettingsStore::save(g_settings);
  sendOk();
}

static void handleConfigExport() {
  g_settings.toConf(s_buf, sizeof s_buf);
  s_server.sendHeader("Content-Disposition", "attachment; filename=\"pcpower.conf\"");
  s_server.sendHeader("Cache-Control", "no-store");
  s_server.send(200, "text/plain", s_buf);
}

static void handleConfigImport() {
  if (!s_server.hasArg("conf")) {
    sendError(400, "no configuration supplied");
    return;
  }
  char err[96] = {0};
  const int applied = g_settings.applyConf(s_server.arg("conf").c_str(), err, sizeof err);
  if (applied < 0) {
    sendError(400, err[0] ? err : "could not read the configuration");
    return;
  }
  applySettingsToHardware();
  SettingsStore::save(g_settings);
  appLogf("config: imported %d settings", applied);
  size_t pos = 0;
  core::jsonAppend(s_buf, sizeof s_buf, &pos, "{\"ok\":true,\"applied\":%d}", applied);
  sendJson(200, s_buf);
}

static void handleDevicesGet() {
  size_t pos = 0;
  core::jsonAppend(s_buf, sizeof s_buf, &pos, "{\"devices\":");
  pos += g_devices.toJson(s_buf + pos, sizeof s_buf - pos, millis());
  core::jsonAppend(s_buf, sizeof s_buf, &pos, "}");
  sendJson(200, s_buf);
}

static void handleDeviceAdd() {
  if (!s_server.hasArg("addr")) {
    sendError(400, "an address is required");
    return;
  }
  uint8_t addr[6];
  if (!core::parseAddress(s_server.arg("addr").c_str(), addr)) {
    sendError(400, "that is not a valid address (expected AA:BB:CC:DD:EE:FF)");
    return;
  }
  long type = 1;
  argInt("type", &type);

  const core::AddrKind kind = core::classifyAddress(addr, (uint8_t)type);
  if (!core::isStable(kind)) {
    sendError(400, core::addrKindReason(kind));
    return;
  }

  const String label = s_server.hasArg("label") ? s_server.arg("label") : String("Device");
  g_devices_locked = true;
  const int index = g_devices.add(addr, (uint8_t)type, label.c_str());
  g_devices_locked = false;
  if (index == -1) {
    sendError(400, "the device list is full");
    return;
  }
  if (index == -2) {
    sendError(400, "that device is already in the list");
    return;
  }
  DeviceStore::save(g_devices);
  appLogf("devices: added %s", label.c_str());
  sendOk();
}

static void handleDeviceUpdate() {
  long index = -1;
  if (!argInt("index", &index) || index < 0 || index >= g_devices.count()) {
    sendError(400, "no such device");
    return;
  }
  g_devices_locked = true;
  if (s_server.hasArg("label")) g_devices.setLabel((uint8_t)index, s_server.arg("label").c_str());
  if (s_server.hasArg("enabled")) {
    long enabled = 1;
    argInt("enabled", &enabled);
    g_devices.setEnabled((uint8_t)index, enabled != 0);
  }
  g_devices_locked = false;
  DeviceStore::save(g_devices);
  sendOk();
}

static void handleDeviceDelete() {
  long index = -1;
  if (!argInt("index", &index) || index < 0 || index >= g_devices.count()) {
    sendError(400, "no such device");
    return;
  }
  appLogf("devices: removed %s", g_devices.at((uint8_t)index).label);
  g_devices_locked = true;
  g_devices.remove((uint8_t)index);
  g_devices_locked = false;
  DeviceStore::save(g_devices);
  sendOk();
}

static void handleLearnStart() {
  long seconds = 5;
  argInt("seconds", &seconds);
  if (seconds < 2) seconds = 2;
  if (seconds > 30) seconds = 30;
  BleScan::startLearning((uint32_t)seconds * 1000);
  sendOk();
}

static void handleLearnCancel() {
  BleScan::cancelLearning();
  sendOk();
}

static void handleLearnStatus() {
  BleScan::learner().toJson(s_buf, sizeof s_buf, millis());
  sendJson(200, s_buf);
}

static void handleLearnAccept() {
  long index = -1;
  if (!argInt("index", &index) || index < 0 || index >= BleScan::learner().count()) {
    sendError(400, "no such candidate");
    return;
  }
  const core::LearnCandidate& c = BleScan::learner().at((uint8_t)index);
  if (!core::isStable(c.kind)) {
    sendError(400, core::addrKindReason(c.kind));
    return;
  }
  String label = s_server.hasArg("label") ? s_server.arg("label") : String("");
  if (label.length() == 0) label = c.name[0] ? String(c.name) : String("Controller");

  g_devices_locked = true;
  const int added = g_devices.add(c.addr, c.addr_type, label.c_str());
  g_devices_locked = false;
  if (added == -1) {
    sendError(400, "the device list is full");
    return;
  }
  if (added == -2) {
    sendError(400, "that device is already in the list");
    return;
  }
  DeviceStore::save(g_devices);
  BleScan::cancelLearning();
  appLogf("devices: learned %s", label.c_str());
  sendOk();
}

static void handleActionPress() {
  const String mode = s_server.hasArg("mode") ? s_server.arg("mode") : String("short");
  const bool ok = mode == "long" ? PowerOut::pulseLong() : PowerOut::pulseShort();
  if (!ok) {
    sendError(409, "a press is already in progress");
    return;
  }
  appLogf("manual: %s press", mode == "long" ? "long" : "short");
  sendOk();
}

static void handleWifiScan() {
  if (!Net::enabled()) {
    sendError(409, "the WiFi radio is off");
    return;
  }
  Net::startScan();
  Net::scanResultsToJson(s_buf, sizeof s_buf);
  sendJson(200, s_buf);
}

static void handleWifiSave() {
  if (!s_server.hasArg("ssid") || s_server.arg("ssid").length() == 0) {
    sendError(400, "a network name is required");
    return;
  }
  Net::saveCredentials(s_server.arg("ssid").c_str(),
                       s_server.hasArg("pass") ? s_server.arg("pass").c_str() : "");
  sendJson(200,
           "{\"ok\":true,\"note\":\"connecting - the hotspot stays up until it works\"}");
}

static void handleWifiForget() {
  Net::forgetCredentials();
  sendOk();
}

static void handleLogs() {
  appLogToJson(s_buf, sizeof s_buf);
  sendJson(200, s_buf);
}

static void handleLogsClear() {
  appLogClear();
  sendOk();
}

static void handleReboot() {
  appLog("reboot requested from the web interface");
  s_reboot_at = millis() + 500;
  sendOk();
}

// --- OTA -------------------------------------------------------------------

static void handleUpdateUpload() {
  HTTPUpload& upload = s_server.upload();

  if (upload.status == UPLOAD_FILE_START) {
    s_ota_rejected = !hostAllowed() || !originAllowed();
    if (s_ota_rejected) {
      appLog("ota: refused, request did not come from this board's own address");
      return;
    }
    s_ota_failed = false;
    s_ota_error[0] = '\0';
    s_ota_written = 0;
    s_ota_percent = 0;
    s_ota_total = s_server.header("Content-Length").toInt();

    // Check this before touching anything else. A board flashed with a single-app-slot
    // partition table can never take an update this way, and saying so plainly beats letting
    // every write fail with "Partition Could Not be Found".
    const esp_partition_t* target = otaTargetPartition();
    if (!target) {
      snprintf(s_ota_error, sizeof s_ota_error,
               "this board has no second app slot, so it cannot be updated over the air. "
               "Reflash it once over USB with the 'No FS 4MB (2MB APP x2)' partition scheme, "
               "or with the -usb.bin from a release, and OTA will work from then on.");
      appLog("ota: refused, this partition table has no second app slot");
      s_ota_failed = true;
      return;
    }

    s_ota_running = true;
    // Give the whole radio and CPU to the upload, and make sure nothing presses the power
    // button while the firmware underneath it is being replaced.
    BleScan::setInhibited(true);
    BleScan::setPaused(true);
    StatusLed::setMode(core::LedMode::WifiConnecting);
    appLogf("ota: receiving %s into %s", upload.filename.c_str(), target->label);

    if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
      snprintf(s_ota_error, sizeof s_ota_error, "%s", Update.errorString());
      appLogf("ota: cannot start (%s)", s_ota_error);
      s_ota_failed = true;
      otaRelease();
      return;
    }
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (s_ota_rejected || s_ota_failed) return;  // drain the upload, do not flood the log
    if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
      snprintf(s_ota_error, sizeof s_ota_error, "%s", Update.errorString());
      appLogf("ota: write failed (%s)", s_ota_error);
      s_ota_failed = true;
      Update.abort();
      otaRelease();
      return;
    }
    s_ota_written += upload.currentSize;
    if (s_ota_total > 0) {
      const size_t percent = s_ota_written * 100 / s_ota_total;
      s_ota_percent = (uint8_t)(percent > 100 ? 100 : percent);
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    if (s_ota_rejected || s_ota_failed) return;
    if (Update.end(true)) {
      s_ota_percent = 100;
      appLogf("ota: %u bytes accepted, rebooting", (unsigned)s_ota_written);
    } else {
      snprintf(s_ota_error, sizeof s_ota_error, "%s", Update.errorString());
      appLogf("ota: failed (%s)", s_ota_error);
      s_ota_failed = true;
      otaRelease();
    }
  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    if (!s_ota_failed) {
      snprintf(s_ota_error, sizeof s_ota_error, "the upload was interrupted");
      s_ota_failed = true;
    }
    Update.abort();
    appLog("ota: aborted");
    otaRelease();
  }
}

static void handleUpdateDone() {
  if (s_ota_rejected) {
    s_ota_rejected = false;
    s_server.send(403, "text/plain", "cross-origin requests are refused");
    return;
  }
  if (s_ota_failed || Update.hasError()) {
    if (s_ota_error[0] == '\0') snprintf(s_ota_error, sizeof s_ota_error, "%s", Update.errorString());
    otaRelease();
    sendError(500, s_ota_error);
    return;
  }
  sendJson(200, "{\"ok\":true}");
  s_reboot_at = millis() + 500;  // let the response reach the browser first
}

static void handleNotFound() {
  // In portal mode every hostname resolves to us, so send stray requests to the page rather
  // than a 404 - that is what makes a phone offer the captive portal.
  if (Net::mode() == Net::Mode::Portal) {
    s_server.sendHeader("Location", String("http://") + Net::ip() + "/", true);
    s_server.send(302, "text/plain", "");
    return;
  }
  s_server.send(404, "text/plain", "not found");
}

// --- wiring ----------------------------------------------------------------

static void route(const char* path, HTTPMethod method, void (*handler)()) {
  s_server.on(path, method, [handler]() {
    if (!requestAllowed()) return;
    handler();
  });
}

namespace Web {

void begin() {
  route("/", HTTP_GET, handleRoot);
  route("/api/status", HTTP_GET, handleStatus);
  route("/api/config", HTTP_GET, handleConfigGet);
  route("/api/config", HTTP_POST, handleConfigPost);
  route("/api/config/schema", HTTP_GET, handleConfigSchema);
  route("/api/config/defaults", HTTP_POST, handleConfigDefaults);
  route("/api/config/export", HTTP_GET, handleConfigExport);
  route("/api/config/import", HTTP_POST, handleConfigImport);
  route("/api/devices", HTTP_GET, handleDevicesGet);
  route("/api/devices", HTTP_POST, handleDeviceAdd);
  route("/api/devices/update", HTTP_POST, handleDeviceUpdate);
  route("/api/devices/delete", HTTP_POST, handleDeviceDelete);
  route("/api/learn/start", HTTP_POST, handleLearnStart);
  route("/api/learn/cancel", HTTP_POST, handleLearnCancel);
  route("/api/learn/status", HTTP_GET, handleLearnStatus);
  route("/api/learn/accept", HTTP_POST, handleLearnAccept);
  route("/api/action/press", HTTP_POST, handleActionPress);
  route("/api/wifi/scan", HTTP_GET, handleWifiScan);
  route("/api/wifi", HTTP_POST, handleWifiSave);
  route("/api/wifi/forget", HTTP_POST, handleWifiForget);
  route("/api/logs", HTTP_GET, handleLogs);
  route("/api/logs/clear", HTTP_POST, handleLogsClear);
  route("/api/reboot", HTTP_POST, handleReboot);
  s_server.on("/update", HTTP_POST, handleUpdateDone, handleUpdateUpload);
  s_server.onNotFound(handleNotFound);

  const char* headers[] = {"Content-Length", "Origin"};
  s_server.collectHeaders(headers, 2);
  s_server.begin();
  appLog("web: listening on port 80");
}

void tick() {
  if (!Net::enabled()) return;
  s_server.handleClient();
  if (s_reboot_at != 0 && (int32_t)(millis() - s_reboot_at) >= 0) {
    Serial.flush();
    ESP.restart();
  }
}

bool otaInProgress() { return s_ota_running; }
uint8_t otaPercent() { return s_ota_percent; }
bool otaCapable() { return otaTargetPartition() != nullptr; }

const char* otaPartition() {
  const esp_partition_t* running = esp_ota_get_running_partition();
  return running ? running->label : "unknown";
}

}  // namespace Web

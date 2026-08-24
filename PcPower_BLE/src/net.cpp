#include "net.h"

#include <DNSServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <WiFi.h>

#include "app.h"
#include "core/json_out.h"
#include "core/radio_policy.h"

static const char* kNamespace = "wifi";

static Net::Mode s_mode = Net::Mode::Off;
static core::ConnectBudget s_budget;
static bool s_enabled = false;
static DNSServer s_dns;
static bool s_dns_running = false;
static bool s_ap_active = false;
static bool s_mdns_started = false;
static uint32_t s_last_attempt_ms = 0;
static uint32_t s_station_since_ms = 0;

static char s_ssid[33] = {0};
static char s_pass[65] = {0};
static char s_ap_ssid[33] = {0};
static char s_hostname[33] = "pcpower";
static char s_ip[16] = "0.0.0.0";

static bool s_scan_running = false;

static void loadCredentials() {
  Preferences prefs;
  if (!prefs.begin(kNamespace, true)) return;
  prefs.getString("ssid", s_ssid, sizeof s_ssid);
  prefs.getString("pass", s_pass, sizeof s_pass);
  prefs.end();
}

static void buildApSsid(const core::Settings& s) {
  const char* configured = s.str(core::S_AP_SSID);
  if (configured && configured[0]) {
    snprintf(s_ap_ssid, sizeof s_ap_ssid, "%s", configured);
    return;
  }
  uint8_t mac[6];
  WiFi.macAddress(mac);
  snprintf(s_ap_ssid, sizeof s_ap_ssid, "PcPower-%02X%02X", mac[4], mac[5]);
}

static void startAp() {
  if (s_ap_active) return;
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(s_ap_ssid, g_settings.str(core::S_AP_PASS));
  s_ap_active = true;
  const IPAddress ip = WiFi.softAPIP();
  s_dns.setErrorReplyCode(DNSReplyCode::NoError);
  s_dns.start(53, "*", ip);  // captive portal: every lookup lands on us
  s_dns_running = true;
  appLogf("wifi: hotspot '%s' up at %s", s_ap_ssid, ip.toString().c_str());
}

static void stopAp() {
  if (!s_ap_active) return;
  if (s_dns_running) {
    s_dns.stop();
    s_dns_running = false;
  }
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);  // running AP and STA together costs airtime the BLE scan wants back
  s_ap_active = false;
  appLog("wifi: hotspot closed, station connection is stable");
}

static void beginConnect() {
  const uint32_t now = millis();
  s_mode = Net::Mode::Connecting;
  s_budget.begin((uint8_t)g_settings.num(core::S_WIFI_TRIES),
                 (uint32_t)g_settings.num(core::S_WIFI_WINDOW_S) * 1000);
  s_budget.start(now);
  WiFi.begin(s_ssid, s_pass);
  s_budget.recordAttempt(now);
  appLogf("wifi: connecting to '%s', %d tries over %ds", s_ssid,
          (int)g_settings.num(core::S_WIFI_TRIES), (int)g_settings.num(core::S_WIFI_WINDOW_S));
}

static void onConnected() {
  s_mode = Net::Mode::Station;
  s_station_since_ms = millis();
  snprintf(s_ip, sizeof s_ip, "%s", WiFi.localIP().toString().c_str());
  if (!s_mdns_started && MDNS.begin(s_hostname)) {
    MDNS.addService("http", "tcp", 80);
    s_mdns_started = true;
  }
  appLogf("wifi: connected, %s (http://%s.local/)", s_ip, s_hostname);
}

namespace Net {

void begin(const core::Settings& s) {
  snprintf(s_hostname, sizeof s_hostname, "%s", s.str(core::S_HOSTNAME));
  WiFi.persistent(false);
  loadCredentials();
  buildApSsid(s);
  WiFi.mode(WIFI_OFF);
  s_enabled = false;
  s_mode = Mode::Off;
  // The radio stays off until Radio::tick() decides it is wanted. In shared mode that is
  // immediately; in exclusive mode it is when the PC comes up.
}

void resume() {
  if (s_enabled) return;
  s_enabled = true;
  WiFi.setHostname(s_hostname);
  WiFi.mode(WIFI_STA);  // startAp() switches to AP_STA if and when a hotspot is needed
  WiFi.setSleep(true);  // BLE and WiFi share one antenna on the C3
  s_ap_active = false;
  s_mdns_started = false;

  if (s_ssid[0]) {
    beginConnect();
  } else {
    s_mode = Mode::Portal;
    startAp();
    appLog("wifi: no credentials stored, waiting on the hotspot");
  }
}

void shutdown() {
  if (!s_enabled) return;
  if (s_mdns_started) {
    MDNS.end();
    s_mdns_started = false;
  }
  if (s_dns_running) {
    s_dns.stop();
    s_dns_running = false;
  }
  if (s_ap_active) {
    WiFi.softAPdisconnect(true);
    s_ap_active = false;
  }
  WiFi.disconnect(true, false);
  WiFi.mode(WIFI_OFF);
  s_enabled = false;
  s_mode = Mode::Off;
  appLog("wifi: radio off, the scanner has the antenna");
}

bool enabled() { return s_enabled; }

void tick() {
  if (!s_enabled) return;
  if (s_dns_running) s_dns.processNextRequest();

  const uint32_t now = millis();
  const bool connected = WiFi.status() == WL_CONNECTED;

  switch (s_mode) {
    case Mode::Off:
    case Mode::Booting:
      break;

    case Mode::Connecting:
      if (connected) {
        onConnected();
      } else if (s_budget.exhausted(now)) {
        appLogf("wifi: %u attempts failed, opening the hotspot",
                (unsigned)s_budget.attempts());
        s_mode = Mode::Portal;
        s_last_attempt_ms = now;
        startAp();
      } else if (s_budget.shouldAttempt(now)) {
        WiFi.disconnect();
        WiFi.begin(s_ssid, s_pass);
        s_budget.recordAttempt(now);
        appLogf("wifi: attempt %u of %d", (unsigned)s_budget.attempts(),
                (int)g_settings.num(core::S_WIFI_TRIES));
      }
      break;

    case Mode::Station:
      if (!connected) {
        appLog("wifi: connection lost");
        beginConnect();
      } else if (s_ap_active) {
        // The hotspot lingers after the station connects, so whoever configured it over the
        // portal can see that it worked before their phone drops back to the house WiFi.
        // A timeout of 0 means they want to keep it up for good.
        const uint32_t linger_ms = (uint32_t)g_settings.num(core::S_AP_TIMEOUT_S) * 1000;
        if (linger_ms > 0 && now - s_station_since_ms > linger_ms) stopAp();
      }
      break;

    case Mode::Portal: {
      if (connected) {
        onConnected();
        break;
      }
      const uint32_t retry_ms = (uint32_t)g_settings.num(core::S_WIFI_RETRY_S) * 1000;
      if (s_ssid[0] && now - s_last_attempt_ms > retry_ms) {
        s_last_attempt_ms = now;
        appLog("wifi: retrying the stored network, hotspot stays up");
        beginConnect();  // the hotspot is left running throughout
      }
      break;
    }
  }
}

Mode mode() { return s_mode; }

const char* modeName() {
  switch (s_mode) {
    case Mode::Off: return "off";
    case Mode::Booting: return "booting";
    case Mode::Connecting: return "connecting";
    case Mode::Station: return "station";
    case Mode::Portal: return "portal";
  }
  return "unknown";
}

bool staConnected() { return s_enabled && WiFi.status() == WL_CONNECTED; }
const char* ip() { return staConnected() ? s_ip : "192.168.4.1"; }
const char* ssid() { return s_ssid; }
int rssi() { return staConnected() ? WiFi.RSSI() : 0; }
bool apActive() { return s_ap_active; }
const char* apSsid() { return s_ap_ssid; }
const char* hostname() { return s_hostname; }
bool hasCredentials() { return s_ssid[0] != 0; }

bool saveCredentials(const char* ssid_in, const char* pass_in) {
  if (!ssid_in || !ssid_in[0]) return false;
  snprintf(s_ssid, sizeof s_ssid, "%s", ssid_in);
  snprintf(s_pass, sizeof s_pass, "%s", pass_in ? pass_in : "");

  Preferences prefs;
  if (prefs.begin(kNamespace, false)) {
    prefs.putString("ssid", s_ssid);
    prefs.putString("pass", s_pass);
    prefs.end();
  }

  // Keep the hotspot up until the new connection is confirmed - a typo must not lock anyone out.
  startAp();
  beginConnect();
  return true;
}

void forgetCredentials() {
  s_ssid[0] = 0;
  s_pass[0] = 0;
  Preferences prefs;
  if (prefs.begin(kNamespace, false)) {
    prefs.clear();
    prefs.end();
  }
  WiFi.disconnect(false, true);
  s_mode = Mode::Portal;
  startAp();
  appLog("wifi: credentials forgotten");
}

void startScan() {
  if (s_scan_running) return;
  WiFi.scanDelete();
  WiFi.scanNetworks(/*async=*/true, /*show_hidden=*/false);
  s_scan_running = true;
}

size_t scanResultsToJson(char* buf, size_t len) {
  size_t pos = 0;
  const int16_t status = WiFi.scanComplete();
  if (status == WIFI_SCAN_RUNNING) {
    core::jsonAppend(buf, len, &pos, "{\"scanning\":true,\"networks\":[]}");
    return pos;
  }
  if (status == WIFI_SCAN_FAILED) {
    startScan();
    core::jsonAppend(buf, len, &pos, "{\"scanning\":true,\"networks\":[]}");
    return pos;
  }
  s_scan_running = false;
  core::jsonAppend(buf, len, &pos, "{\"scanning\":false,\"networks\":[");
  for (int i = 0; i < status; ++i) {
    core::jsonAppend(buf, len, &pos, "%s{\"ssid\":\"", i ? "," : "");
    core::jsonAppendEscaped(buf, len, &pos, WiFi.SSID(i).c_str());
    core::jsonAppend(buf, len, &pos, "\",\"rssi\":%d,\"secure\":%s}", (int)WiFi.RSSI(i),
                     WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "false" : "true");
  }
  core::jsonAppend(buf, len, &pos, "]}");
  return pos;
}

size_t statusToJson(char* buf, size_t len) {
  size_t pos = 0;
  core::jsonAppend(buf, len, &pos, "{\"enabled\":%s,\"mode\":\"%s\",\"ssid\":\"",
                   s_enabled ? "true" : "false", modeName());
  core::jsonAppendEscaped(buf, len, &pos, s_ssid);
  core::jsonAppend(buf, len, &pos,
                   "\",\"ip\":\"%s\",\"rssi\":%d,\"ap_active\":%s,\"ap_ssid\":\"%s\","
                   "\"hostname\":\"%s\",\"has_credentials\":%s}",
                   ip(), rssi(), s_ap_active ? "true" : "false", s_ap_ssid, s_hostname,
                   hasCredentials() ? "true" : "false");
  return pos;
}

}  // namespace Net

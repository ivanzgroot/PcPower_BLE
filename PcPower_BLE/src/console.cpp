#include "console.h"

#include "app.h"
#include "ble_scan.h"
#include "device_store.h"
#include "net.h"
#include "pc_sense.h"
#include "power_out.h"
#include "settings_store.h"
#include "status_led.h"
#include "diagnostics.h"
#include "radio.h"
#include "web_server.h"

static char s_line[160];
static size_t s_len = 0;

static void printHelp() {
  Serial.println(F(
      "commands:\n"
      "  status              what the board thinks is going on\n"
      "  log                 the last 64 log lines\n"
      "  clearlog            empty the log\n"
      "  devices             the known-device list\n"
      "  learn [seconds]     capture the strongest advertiser (default 5 s)\n"
      "  accept [i] [label]  add candidate i from the last learn\n"
      "  forget <i>          remove device i\n"
      "  enable <i> <0|1>    enable or disable device i\n"
      "  press               short press of the power button\n"
      "  longpress           hold the button (force off)\n"
      "  wifi <ssid> <pass>  join a network\n"
      "  wififorget          erase the stored network\n"
      "  get [key]           show one setting, or all of them\n"
      "  set <key> <value>   change a setting\n"
      "  defaults            restore settings (keeps devices and WiFi)\n"
      "  reboot              restart the board\n"
      "  version             firmware version"));
}

static void printStatus() {
  Serial.printf("version   : %s, up %lu s, heap %u\n", kVersion,
                (unsigned long)(millis() / 1000), (unsigned)ESP.getFreeHeap());
  Serial.printf("pc        : %s (duty %u%%, spread %u%%, %s)\n",
                core::pcStateName(PcSense::state()), (unsigned)PcSense::dutyPct(),
                (unsigned)PcSense::spreadPct(), PcSense::ready() ? "ready" : "warming up");
  Serial.printf("scan      : %s%s, %u adverts seen\n",
                BleScan::scanning() ? "running" : "stopped",
                BleScan::paused() ? " (paused)" : "", (unsigned)BleScan::advertsSeen());
  Serial.printf("last check: %s\n", core::triggerReasonText(BleScan::lastReason()));
  Serial.printf("presses   : %u\n", (unsigned)PowerOut::pulseCount());
  Serial.printf("radio     : %s, antenna belongs to %s\n",
                Radio::exclusive() ? "exclusive" : "shared",
                core::radioOwnerName(Radio::owner()));
  if (Net::enabled()) {
    Serial.printf("wifi      : %s, ssid '%s', ip %s%s\n", Net::modeName(), Net::ssid(), Net::ip(),
                  Net::apActive() ? ", hotspot up" : "");
  } else {
    Serial.printf("wifi      : off (ssid '%s' stored)\n", Net::ssid());
  }
  Serial.printf("devices   : %u known\n", (unsigned)g_devices.count());
  Serial.printf("memory    : %u free now, %u free at the lowest, largest block %u\n",
                (unsigned)ESP.getFreeHeap(), (unsigned)Diag::minFreeHeapBytes(),
                (unsigned)Diag::largestFreeBlockBytes());
  Serial.printf("ota       : running from %s, %s\n", Web::otaPartition(),
                Web::otaCapable() ? "a spare slot is available"
                                  : "NO spare slot - this board cannot update over the air");
}

static void printDevices() {
  if (g_devices.count() == 0) {
    Serial.println(F("no devices learned yet - run 'learn'"));
    return;
  }
  for (uint8_t i = 0; i < g_devices.count(); ++i) {
    const core::Device& d = g_devices.at(i);
    char addr[18];
    core::formatAddress(d.addr, addr, sizeof addr);
    Serial.printf("%u  %-17s %-3s %-20s rssi %d, %u triggers%s\n", (unsigned)i, addr,
                  d.enabled ? "on" : "off", d.label, (int)d.last_rssi,
                  (unsigned)d.trigger_count, d.last_seen_ms ? "" : ", not seen this boot");
  }
}

static void printLog() {
  for (size_t i = 0; i < g_log.count(); ++i) {
    Serial.printf("[%8lu] %s\n", (unsigned long)g_log.timestamp(i), g_log.line(i));
  }
}

// Splits off the next whitespace-delimited word, advancing *cursor past it.
static char* nextWord(char** cursor) {
  char* s = *cursor;
  while (*s == ' ' || *s == '\t') ++s;
  if (*s == '\0') {
    *cursor = s;
    return nullptr;
  }
  char* start = s;
  while (*s && *s != ' ' && *s != '\t') ++s;
  if (*s) {
    *s = '\0';
    ++s;
  }
  *cursor = s;
  return start;
}

static void handleSet(char* rest) {
  char* key = nextWord(&rest);
  if (!key) {
    Serial.println(F("usage: set <key> <value>"));
    return;
  }
  while (*rest == ' ') ++rest;
  if (*rest == '\0') {
    Serial.println(F("usage: set <key> <value>"));
    return;
  }
  char err[96] = {0};
  bool clamped = false;
  if (!g_settings.setByKey(key, rest, err, sizeof err, &clamped)) {
    Serial.printf("error: %s\n", err);
    return;
  }
  g_settings.applyCoupling();
  PowerOut::reconfigure(g_settings);
  PcSense::reconfigure(g_settings);
  StatusLed::reconfigure(g_settings);
  BleScan::reconfigure(g_settings);
  SettingsStore::save(g_settings);

  core::SettingId id;
  const core::SettingDef* def = core::Settings::findDef(key, &id);
  if (def && def->type == core::SettingType::Str) {
    Serial.printf("%s=%s\n", key, g_settings.str(id));
  } else if (def) {
    Serial.printf("%s=%d%s\n", key, (int)g_settings.num(id),
                  clamped ? "  (clamped into range)" : "");
  }
}

static void handleGet(char* rest) {
  char* key = nextWord(&rest);
  if (!key) {
    static char buf[6144];
    g_settings.toConf(buf, sizeof buf);
    Serial.print(buf);
    return;
  }
  core::SettingId id;
  const core::SettingDef* def = core::Settings::findDef(key, &id);
  if (!def) {
    Serial.printf("unknown setting '%s'\n", key);
    return;
  }
  if (def->type == core::SettingType::Str) {
    Serial.printf("%s=%s\n", key, g_settings.str(id));
  } else {
    Serial.printf("%s=%d  (%d..%d) %s\n", key, (int)g_settings.num(id), (int)def->min,
                  (int)def->max, def->unit);
  }
}

static void handleAccept(char* rest) {
  char* index_text = nextWord(&rest);
  const long index = index_text ? strtol(index_text, nullptr, 10) : 0;
  if (index < 0 || index >= BleScan::learner().count()) {
    Serial.println(F("no such candidate - run 'learn' first"));
    return;
  }
  const core::LearnCandidate& c = BleScan::learner().at((uint8_t)index);
  if (!core::isStable(c.kind)) {
    Serial.printf("refused: %s\n", core::addrKindReason(c.kind));
    return;
  }
  while (*rest == ' ') ++rest;
  const char* label = *rest ? rest : (c.name[0] ? c.name : "Controller");
  if (g_devices_mutex) xSemaphoreTake(g_devices_mutex, portMAX_DELAY);
  const int added = g_devices.add(c.addr, c.addr_type, label);
  if (g_devices_mutex) xSemaphoreGive(g_devices_mutex);
  if (added < 0) {
    Serial.println(F("could not add (list full, or already known)"));
    return;
  }
  DeviceStore::save(g_devices);
  Serial.printf("added %s\n", label);
}

static void printLearnCandidates() {
  const core::Learner& l = BleScan::learner();
  if (l.count() == 0) {
    Serial.println(F("nothing heard"));
    return;
  }
  for (uint8_t i = 0; i < l.count(); ++i) {
    const core::LearnCandidate& c = l.at(i);
    char addr[18];
    core::formatAddress(c.addr, addr, sizeof addr);
    Serial.printf("%u  %s  rssi %d  %-14s %s%s\n", (unsigned)i, addr, (int)c.best_rssi,
                  core::addrKindName(c.kind), c.name[0] ? c.name : "(no name)",
                  core::isStable(c.kind) ? "" : "  [refused: rotating address]");
  }
  Serial.println(F("use 'accept <i> [label]' to add one"));
}

static void execute(char* line) {
  char* cursor = line;
  char* cmd = nextWord(&cursor);
  if (!cmd) return;

  if (!strcmp(cmd, "help") || !strcmp(cmd, "?")) {
    printHelp();
  } else if (!strcmp(cmd, "status")) {
    printStatus();
  } else if (!strcmp(cmd, "log")) {
    printLog();
  } else if (!strcmp(cmd, "clearlog")) {
    appLogClear();
    Serial.println(F("log cleared"));
  } else if (!strcmp(cmd, "devices")) {
    printDevices();
  } else if (!strcmp(cmd, "learn")) {
    char* seconds_text = nextWord(&cursor);
    long seconds = seconds_text ? strtol(seconds_text, nullptr, 10) : 5;
    if (seconds < 2) seconds = 2;
    if (seconds > 30) seconds = 30;
    Serial.printf("listening for %ld s - switch the device on now\n", seconds);
    BleScan::startLearning((uint32_t)seconds * 1000);
    const uint32_t until = millis() + (uint32_t)seconds * 1000 + 200;
    while ((int32_t)(millis() - until) < 0) {
      BleScan::tick();
      delay(20);
    }
    printLearnCandidates();
  } else if (!strcmp(cmd, "accept")) {
    handleAccept(cursor);
  } else if (!strcmp(cmd, "forget")) {
    char* index_text = nextWord(&cursor);
    const long index = index_text ? strtol(index_text, nullptr, 10) : -1;
    if (index < 0 || index >= g_devices.count()) {
      Serial.println(F("usage: forget <index>  (see 'devices')"));
      return;
    }
    if (g_devices_mutex) xSemaphoreTake(g_devices_mutex, portMAX_DELAY);
    Serial.printf("removed %s\n", g_devices.at((uint8_t)index).label);
    g_devices.remove((uint8_t)index);
    if (g_devices_mutex) xSemaphoreGive(g_devices_mutex);
    DeviceStore::save(g_devices);
  } else if (!strcmp(cmd, "enable")) {
    char* index_text = nextWord(&cursor);
    char* value_text = nextWord(&cursor);
    const long index = index_text ? strtol(index_text, nullptr, 10) : -1;
    if (index < 0 || index >= g_devices.count() || !value_text) {
      Serial.println(F("usage: enable <index> <0|1>"));
      return;
    }
    if (g_devices_mutex) xSemaphoreTake(g_devices_mutex, portMAX_DELAY);
    g_devices.setEnabled((uint8_t)index, strtol(value_text, nullptr, 10) != 0);
    if (g_devices_mutex) xSemaphoreGive(g_devices_mutex);
    DeviceStore::save(g_devices);
    Serial.println(F("ok"));
  } else if (!strcmp(cmd, "press")) {
    Serial.println(PowerOut::pulseShort() ? F("pressed") : F("a press is already running"));
  } else if (!strcmp(cmd, "longpress")) {
    Serial.println(PowerOut::pulseLong() ? F("holding") : F("a press is already running"));
  } else if (!strcmp(cmd, "wifi")) {
    char* ssid = nextWord(&cursor);
    if (!ssid) {
      Serial.println(F("usage: wifi <ssid> <password>"));
      return;
    }
    while (*cursor == ' ') ++cursor;
    Net::saveCredentials(ssid, cursor);
    Serial.println(F("connecting - the hotspot stays up until it works"));
  } else if (!strcmp(cmd, "wififorget")) {
    Net::forgetCredentials();
    Serial.println(F("forgotten"));
  } else if (!strcmp(cmd, "get")) {
    handleGet(cursor);
  } else if (!strcmp(cmd, "set")) {
    handleSet(cursor);
  } else if (!strcmp(cmd, "defaults")) {
    SettingsStore::restoreDefaults(g_settings);
    PowerOut::reconfigure(g_settings);
    PcSense::reconfigure(g_settings);
    StatusLed::reconfigure(g_settings);
    BleScan::reconfigure(g_settings);
    SettingsStore::save(g_settings);
    Serial.println(F("settings restored - devices and WiFi kept"));
  } else if (!strcmp(cmd, "reboot")) {
    Serial.println(F("rebooting"));
    Serial.flush();
    ESP.restart();
  } else if (!strcmp(cmd, "version")) {
    Serial.println(kVersion);
  } else {
    Serial.printf("unknown command '%s' - try 'help'\n", cmd);
  }
}

namespace Console {

void begin() {
  Serial.println(F("type 'help' for commands"));
}

void tick() {
  while (Serial.available()) {
    const char c = (char)Serial.read();
    if (c == '\r') continue;
    if (c == '\n') {
      s_line[s_len] = '\0';
      if (s_len > 0) execute(s_line);
      s_len = 0;
      continue;
    }
    if (s_len < sizeof s_line - 1) s_line[s_len++] = c;
  }
}

}  // namespace Console

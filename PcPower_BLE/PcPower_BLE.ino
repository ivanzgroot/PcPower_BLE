// PcPower_BLE - wakes a PC when a known BLE controller is switched on.
//
// Hardware: ESP32-C3 SuperMini. See docs/wiring.md.
//   GPIO5 -> G3VM-61A1 input LED, output across the PC power button (active HIGH)
//   GPIO3 <- G3VM-61A1 output driven by the PC PWR-LED          (LOW = LED lit)
//   GPIO8 -> on-board status LED                                (active LOW)
//
// Every pin and polarity above is a runtime setting; these are only the defaults.
#include <Arduino.h>
#include <esp_system.h>

#include "src/app.h"
#include "src/ble_scan.h"
#include "src/console.h"
#include "src/device_store.h"
#include "src/net.h"
#include "src/pc_sense.h"
#include "src/power_out.h"
#include "src/radio.h"
#include "src/settings_store.h"
#include "src/status_led.h"
#include "src/web_server.h"

// Used before settings are loaded from NVS. Must match the defaults in settings_model.cpp.
static constexpr uint8_t kBootPinOut = 5;
static constexpr bool kBootOutHigh = true;

void setup() {
  // Boot-glitch protection, first statement in the program: the ESP32-C3's GPIOs float from
  // reset until this runs, so the board also needs an external 10k pulldown on the output pin.
  // See README - without it the PC can twitch every time the ESP reboots.
  digitalWrite(kBootPinOut, kBootOutHigh ? LOW : HIGH);
  pinMode(kBootPinOut, OUTPUT);
  digitalWrite(kBootPinOut, kBootOutHigh ? LOW : HIGH);

  Serial.begin(115200);
  Serial.setTxTimeoutMs(0);  // with USB-CDC an unattached host would otherwise block writes

  appBegin();
  SettingsStore::load(g_settings);
  DeviceStore::load(g_devices);

  StatusLed::begin(g_settings);
  StatusLed::setMode(core::LedMode::Boot);
  StatusLed::tick();

  PowerOut::begin(g_settings);
  PcSense::begin(g_settings);

  Serial.printf("\nPcPower_BLE %s\n", kVersion);
  // A dropped upload looks identical whether the network died or the board did. This is how
  // you tell: a panic or watchdog reset here means the board restarted mid-transfer.
  appLogf("boot: reset reason %d (%s)", (int)esp_reset_reason(),
          esp_reset_reason() == ESP_RST_POWERON   ? "power-on"
          : esp_reset_reason() == ESP_RST_SW      ? "software restart"
          : esp_reset_reason() == ESP_RST_PANIC   ? "PANIC - firmware crashed"
          : esp_reset_reason() == ESP_RST_INT_WDT ? "interrupt watchdog"
          : esp_reset_reason() == ESP_RST_TASK_WDT ? "task watchdog"
          : esp_reset_reason() == ESP_RST_BROWNOUT ? "BROWNOUT - power supply dipped"
                                                   : "other");
  appLogf("boot: v%s, %u known devices, out=GPIO%d sense=GPIO%d", kVersion,
          (unsigned)g_devices.count(), (int)g_settings.num(core::S_PIN_OUT),
          (int)g_settings.num(core::S_PIN_SENSE));

  delay(500);  // the only delay in the firmware: hold the boot self-test light long enough to see

  BleScan::begin(g_settings);
  Net::begin(g_settings);   // sets up, but leaves the radio off
  Radio::begin(g_settings); // decides from here on which radio is powered
  Console::begin();
}

// Picks the LED pattern from what the board is actually doing.
static void updateStatusLed() {
  if (PowerOut::active()) {
    StatusLed::overlay(core::LedMode::PulseActive, 50);
  } else if (Web::otaInProgress() || Net::mode() == Net::Mode::Connecting) {
    StatusLed::setMode(core::LedMode::WifiConnecting);
  } else if (Net::mode() == Net::Mode::Portal) {
    StatusLed::setMode(core::LedMode::Portal);
  } else if (BleScan::learning()) {
    StatusLed::setMode(core::LedMode::Learning);
    StatusLed::tick();
    return;
  } else if (PcSense::state() == core::PcState::On) {
    StatusLed::setMode(core::LedMode::PcOnIdle);
  } else {
    StatusLed::setMode(core::LedMode::ArmedScanning);
  }
  StatusLed::tick();
}

void loop() {
  // Hands the antenna to whichever radio is useful right now, and powers the other one down.
  Radio::tick();

  Net::tick();   // both return immediately while the WiFi hardware is off
  Web::tick();
  Console::tick();

  if (!Web::otaInProgress()) BleScan::tick();

  updateStatusLed();
  appLogPump();

  // An upload gets the whole loop; every millisecond spent elsewhere is a millisecond the
  // browser spends waiting, and the server drops a stalled transfer after five seconds.
  if (!Web::otaInProgress()) delay(2);
}

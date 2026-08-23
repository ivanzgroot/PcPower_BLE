// PcPower_BLE - wakes a PC when a known BLE controller is switched on.
//
// Hardware: ESP32-C3 SuperMini. See docs/wiring.md.
//   GPIO5 -> G3VM-61A1 input LED, output across the PC power button (active HIGH)
//   GPIO3 <- G3VM-61A1 output driven by the PC PWR-LED          (LOW = LED lit)
//   GPIO8 -> on-board status LED                                (active LOW)
//
// Every pin and polarity above is a runtime setting; these are only the defaults.
#include <Arduino.h>

#include "src/app.h"
#include "src/ble_scan.h"
#include "src/device_store.h"
#include "src/pc_sense.h"
#include "src/power_out.h"
#include "src/settings_store.h"
#include "src/status_led.h"

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

  appLogBegin();
  SettingsStore::load(g_settings);
  DeviceStore::load(g_devices);

  StatusLed::begin(g_settings);
  StatusLed::setMode(core::LedMode::Boot);
  StatusLed::tick();

  PowerOut::begin(g_settings);
  PcSense::begin(g_settings);

  Serial.printf("\nPcPower_BLE %s\n", kVersion);
  appLogf("boot: v%s, %u known devices, out=GPIO%d sense=GPIO%d", kVersion,
          (unsigned)g_devices.count(), (int)g_settings.num(core::S_PIN_OUT),
          (int)g_settings.num(core::S_PIN_SENSE));

  delay(500);  // the only delay in the firmware: hold the boot self-test light long enough to see

  BleScan::begin(g_settings);
}

// Picks the LED pattern from what the board is actually doing.
static void updateStatusLed() {
  if (PowerOut::active()) {
    StatusLed::overlay(core::LedMode::PulseActive, 50);
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
  // Nothing can trigger while the PC runs, so give the antenna back to WiFi.
  const bool pc_on = PcSense::state() == core::PcState::On;
  BleScan::setPaused(pc_on && g_settings.flag(core::S_PAUSE_WHEN_ON));

  BleScan::tick();
  updateStatusLed();
  appLogPump();

  delay(5);  // yield to the radio and the sense task
}

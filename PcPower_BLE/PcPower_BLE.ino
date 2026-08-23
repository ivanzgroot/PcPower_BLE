// PcPower_BLE - wakes a PC when a known BLE controller is switched on.
//
// Hardware: ESP32-C3 SuperMini. See docs/wiring.md.
//   GPIO5 -> G3VM-61A1 input LED, output across the PC power button (active HIGH)
//   GPIO3 <- G3VM-61A1 output driven by the PC PWR-LED          (LOW = LED lit)
//   GPIO8 -> on-board status LED                                (active LOW)
#include <Arduino.h>

#include "src/app.h"
#include "src/device_store.h"
#include "src/settings_store.h"

// Compile-time fallbacks used before settings are loaded.
static constexpr uint8_t kPinOut  = 5;
static constexpr bool    kOutHigh = true;
static constexpr uint8_t kPinLed  = 8;
static constexpr bool    kLedLow  = true;

void setup() {
  // Boot-glitch protection: idle the power output before anything else runs.
  // GPIO5 floats from reset until this line, which is why the board also needs
  // an external 10k pulldown - see README.
  digitalWrite(kPinOut, kOutHigh ? LOW : HIGH);
  pinMode(kPinOut, OUTPUT);
  digitalWrite(kPinOut, kOutHigh ? LOW : HIGH);

  pinMode(kPinLed, OUTPUT);
  digitalWrite(kPinLed, kLedLow ? LOW : HIGH);  // solid = boot self-test

  Serial.begin(115200);
  Serial.setTxTimeoutMs(0);  // never block when no USB host is attached
  delay(500);
  digitalWrite(kPinLed, kLedLow ? HIGH : LOW);
  Serial.printf("\nPcPower_BLE %s booting\n", kVersion);

  appLogBegin();
  SettingsStore::load(g_settings);
  DeviceStore::load(g_devices);
  appLogf("boot: v%s, %u known devices, out=GPIO%d sense=GPIO%d", kVersion,
          (unsigned)g_devices.count(), (int)g_settings.num(core::S_PIN_OUT),
          (int)g_settings.num(core::S_PIN_SENSE));
}

void loop() {
  static uint32_t last = 0;
  if (millis() - last >= 3000) {
    last = millis();
    digitalWrite(kPinLed, kLedLow ? LOW : HIGH);
    delay(40);
    digitalWrite(kPinLed, kLedLow ? HIGH : LOW);
  }
  appLogPump();
}

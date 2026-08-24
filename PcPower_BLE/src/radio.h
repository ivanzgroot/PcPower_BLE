// Decides which radio owns the antenna, and makes it so.
//
// The arbitration used to sit inline in loop(); it lives here now because exclusive mode turns
// it into a real state machine with a dwell, an OTA interlock and two subsystems to drive.
#pragma once
#include <Arduino.h>

#include "core/radio_policy.h"
#include "core/settings_model.h"

namespace Radio {
void begin(const core::Settings& s);
void tick();

core::RadioOwner owner();
bool exclusive();          // configured on AND usable (a forced-off sense disables it)
bool wifiEnabled();
bool bleScanning();
size_t toJson(char* buf, size_t len);
}  // namespace Radio

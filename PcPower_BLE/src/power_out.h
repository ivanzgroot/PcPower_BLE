// Drives the opto-isolator wired across the PC's power button.
//
// Pulse timing comes from an esp_timer, never from loop(), so a busy web request or an OTA
// upload cannot stretch a 300 ms press into a six second one.
#pragma once
#include <Arduino.h>

#include "core/settings_model.h"

namespace PowerOut {
void begin(const core::Settings& s);
void reconfigure(const core::Settings& s);  // idles the old pin before adopting a new one
bool pulse(uint32_t ms);                    // false when a pulse is already running
bool pulseShort();                          // pulse_ms from settings
bool pulseLong();                           // long_press_ms from settings - the force-off
bool active();
uint32_t msSinceLastPulse();  // UINT32_MAX when nothing has pulsed yet
uint32_t pulseCount();
}  // namespace PowerOut

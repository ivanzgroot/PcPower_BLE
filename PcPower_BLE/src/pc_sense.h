// Reads the PC's power LED through the second opto-isolator and reports on / off / sleep.
#pragma once
#include <Arduino.h>

#include "core/led_classifier.h"
#include "core/settings_model.h"

namespace PcSense {
void begin(const core::Settings& s);
void reconfigure(const core::Settings& s);
core::PcState state();  // always Off when sense_mode is force_off
core::PcState rawState();
uint8_t dutyPct();
uint8_t spreadPct();
bool litNow();
bool ready();
uint32_t msSinceOffEdge();  // since the last on -> off/sleep change, UINT32_MAX if never on
uint32_t msSinceOnEdge();
}  // namespace PcSense

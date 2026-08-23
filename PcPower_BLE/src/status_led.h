// The on-board LED: six states, readable across a room.
#pragma once
#include <Arduino.h>

#include "core/led_pattern.h"
#include "core/settings_model.h"

namespace StatusLed {
void begin(const core::Settings& s);
void reconfigure(const core::Settings& s);
void setMode(core::LedMode mode);
core::LedMode mode();
void overlay(core::LedMode mode, uint32_t ms);  // temporary, e.g. solid while a pulse fires
void tick();                                    // call from loop()
}  // namespace StatusLed

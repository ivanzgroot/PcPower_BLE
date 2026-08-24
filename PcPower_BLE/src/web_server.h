// The HTTP API, and the page that is only a client of it.
#pragma once
#include <Arduino.h>

namespace Web {
void begin();
void tick();
bool otaInProgress();
uint8_t otaPercent();
}  // namespace Web

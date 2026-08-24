// The HTTP API, and the page that is only a client of it.
#pragma once
#include <Arduino.h>

namespace Web {
void begin();
void tick();
bool otaInProgress();
uint8_t otaPercent();
bool otaCapable();          // false when this partition table has no second app slot
const char* otaPartition();  // label of the slot currently running
}  // namespace Web

// Serial console. Everything here is also available over HTTP - a cable is never required,
// it is just useful when the network is the thing that is broken.
#pragma once
#include <Arduino.h>

namespace Console {
void begin();
void tick();
}  // namespace Console

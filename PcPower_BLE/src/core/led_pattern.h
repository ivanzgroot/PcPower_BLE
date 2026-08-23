// Status LED patterns. Six states the board can be in, each recognisable across the room.
//
// Pure functions of the clock, so the LED never has to remember where it was in a blink and
// the patterns can be verified without hardware.
#pragma once
#include <cstdint>

namespace core {

enum class LedMode : uint8_t {
  Boot,            // solid - the self-test at power-up
  Portal,          // two short blinks every 2 s - hotspot is up, waiting to be configured
  WifiConnecting,  // 5 Hz - joining a network, or busy with an update
  ArmedScanning,   // one 40 ms wink every 3 s - PC is off, listening for a controller
  PcOnIdle,        // one 40 ms wink every 8 s - PC is running, scanning paused
  Learning,        // 10 Hz flicker - capturing a device
  PulseActive,     // solid - the power button is being held right now
  Dark,            // the LED is switched off in settings
};

bool ledLitAt(LedMode mode, uint32_t now_ms);
const char* ledModeName(LedMode mode);

}  // namespace core

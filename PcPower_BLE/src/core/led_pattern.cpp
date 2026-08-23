#include "core/led_pattern.h"

namespace core {

bool ledLitAt(LedMode mode, uint32_t now_ms) {
  switch (mode) {
    case LedMode::Boot:
    case LedMode::PulseActive:
      return true;
    case LedMode::Dark:
      return false;
    case LedMode::Portal: {
      const uint32_t p = now_ms % 2000;
      return p < 80 || (p >= 240 && p < 320);
    }
    case LedMode::WifiConnecting:
      return (now_ms % 200) < 100;
    case LedMode::ArmedScanning:
      return (now_ms % 3000) < 40;
    case LedMode::PcOnIdle:
      return (now_ms % 8000) < 40;
    case LedMode::Learning:
      return (now_ms % 100) < 50;
  }
  return false;
}

const char* ledModeName(LedMode mode) {
  switch (mode) {
    case LedMode::Boot: return "boot";
    case LedMode::Portal: return "portal";
    case LedMode::WifiConnecting: return "wifi-connecting";
    case LedMode::ArmedScanning: return "armed";
    case LedMode::PcOnIdle: return "pc-on";
    case LedMode::Learning: return "learning";
    case LedMode::PulseActive: return "pulse";
    case LedMode::Dark: return "dark";
  }
  return "unknown";
}

}  // namespace core

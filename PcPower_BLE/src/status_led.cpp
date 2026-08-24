#include "status_led.h"

static uint8_t s_pin = 8;
static bool s_active_low = true;
static bool s_enabled = true;
static core::LedMode s_mode = core::LedMode::Boot;
static core::LedMode s_overlay = core::LedMode::Dark;
static uint32_t s_overlay_until = 0;
static bool s_last_lit = false;
static bool s_have_written = false;

static void write(bool lit) {
  if (s_have_written && lit == s_last_lit) return;  // don't hammer the pin every loop
  digitalWrite(s_pin, (lit != s_active_low) ? HIGH : LOW);
  s_last_lit = lit;
  s_have_written = true;
}

namespace StatusLed {

void begin(const core::Settings& s) {
  s_pin = (uint8_t)s.num(core::S_PIN_LED);
  s_active_low = s.flag(core::S_LED_ACTIVE_LOW);
  s_enabled = s.flag(core::S_LED_ENABLED);
  pinMode(s_pin, OUTPUT);
  s_have_written = false;
  write(false);
}

void reconfigure(const core::Settings& s) {
  const uint8_t pin = (uint8_t)s.num(core::S_PIN_LED);
  if (pin != s_pin) {
    write(false);
    pinMode(s_pin, INPUT);
    s_pin = pin;
    pinMode(s_pin, OUTPUT);
    s_have_written = false;
  }
  s_active_low = s.flag(core::S_LED_ACTIVE_LOW);
  s_enabled = s.flag(core::S_LED_ENABLED);
  write(false);
}

void setMode(core::LedMode mode) { s_mode = mode; }
core::LedMode mode() { return s_mode; }

void overlay(core::LedMode mode, uint32_t ms) {
  s_overlay = mode;
  s_overlay_until = millis() + ms;
}

void tick() {
  if (!s_enabled) {
    write(false);
    return;
  }
  const uint32_t now = millis();
  const bool overlay_active = (int32_t)(s_overlay_until - now) > 0;
  const core::LedMode active = overlay_active ? s_overlay : s_mode;
  // Phase comes straight from the clock, so patterns stay continuous across mode changes.
  write(core::ledLitAt(active, now));
}

}  // namespace StatusLed

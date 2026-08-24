#include "power_out.h"

#include <esp_timer.h>

#include "app.h"
#include "core/pulse_timer.h"

static core::PulseTimer s_timer;
static uint8_t s_pin = 5;
static bool s_active_high = true;
static esp_timer_handle_t s_oneshot = nullptr;
static esp_timer_handle_t s_watchdog = nullptr;
static volatile uint32_t s_last_pulse_ms = 0;
static volatile uint32_t s_count = 0;
static volatile bool s_ever_pulsed = false;

static inline void driveIdle() { digitalWrite(s_pin, s_active_high ? LOW : HIGH); }
static inline void driveActive() { digitalWrite(s_pin, s_active_high ? HIGH : LOW); }

static void onPulseEnd(void*) {
  driveIdle();
  s_timer.update(millis());
}

// Belt and braces: if the one-shot ever fails to fire, this releases the button within 250 ms.
// Holding a PC's power button down indefinitely is the one failure worth spending a timer on.
static void onWatchdog(void*) {
  if (!s_timer.active()) driveIdle();
}

namespace PowerOut {

void begin(const core::Settings& s) {
  s_pin = (uint8_t)s.num(core::S_PIN_OUT);
  s_active_high = s.flag(core::S_OUT_ACTIVE_HIGH);
  pinMode(s_pin, OUTPUT);
  driveIdle();

  uint32_t ceiling = (uint32_t)s.num(core::S_LONG_PRESS_MS);
  if (ceiling < 15000) ceiling = 15000;
  s_timer.begin(ceiling);

  if (!s_oneshot) {
    const esp_timer_create_args_t args = {onPulseEnd, nullptr, ESP_TIMER_TASK, "pulse", true};
    esp_timer_create(&args, &s_oneshot);
  }
  if (!s_watchdog) {
    const esp_timer_create_args_t args = {onWatchdog, nullptr, ESP_TIMER_TASK, "pulsewd", true};
    esp_timer_create(&args, &s_watchdog);
    esp_timer_start_periodic(s_watchdog, 250000);
  }
}

void reconfigure(const core::Settings& s) {
  const uint8_t new_pin = (uint8_t)s.num(core::S_PIN_OUT);
  const bool new_high = s.flag(core::S_OUT_ACTIVE_HIGH);
  if (new_pin == s_pin && new_high == s_active_high) return;

  if (s_timer.active()) {
    esp_timer_stop(s_oneshot);
    s_timer.abort();
  }
  driveIdle();          // release the old pin before letting go of it
  pinMode(s_pin, INPUT);
  s_pin = new_pin;
  s_active_high = new_high;
  pinMode(s_pin, OUTPUT);
  driveIdle();
  appLogf("output: now GPIO%u active %s", (unsigned)s_pin, s_active_high ? "high" : "low");
}

bool pulse(uint32_t ms) {
  const uint32_t now = millis();
  if (!s_timer.start(now, ms)) return false;
  s_last_pulse_ms = now;
  s_ever_pulsed = true;
  ++s_count;
  driveActive();
  esp_timer_stop(s_oneshot);
  esp_timer_start_once(s_oneshot, (uint64_t)s_timer.remaining(now) * 1000ULL);
  return true;
}

bool pulseShort() { return pulse((uint32_t)g_settings.num(core::S_PULSE_MS)); }
bool pulseLong() { return pulse((uint32_t)g_settings.num(core::S_LONG_PRESS_MS)); }
bool active() { return s_timer.active(); }
uint32_t pulseCount() { return s_count; }

uint32_t msSinceLastPulse() {
  if (!s_ever_pulsed) return UINT32_MAX;
  return millis() - s_last_pulse_ms;
}

}  // namespace PowerOut

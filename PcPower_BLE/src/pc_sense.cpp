#include "pc_sense.h"

#include "app.h"

static core::LedClassifier s_classifier;
static uint8_t s_pin = 3;
static bool s_active_low = true;
static bool s_force_off = false;
static volatile bool s_lit = false;
static volatile uint32_t s_off_edge_ms = 0;
static volatile bool s_have_off_edge = false;
static volatile uint32_t s_on_edge_ms = 0;
static volatile bool s_reconfigure = false;
static TaskHandle_t s_task = nullptr;

static core::LedTuning tuningFrom(const core::Settings& s) {
  core::LedTuning t;
  t.window_ms = (uint16_t)s.num(core::S_SENSE_WINDOW_MS);
  t.on_duty_pct = (uint8_t)s.num(core::S_ON_DUTY_PCT);
  t.off_duty_pct = (uint8_t)s.num(core::S_OFF_DUTY_PCT);
  t.spread_pct = (uint8_t)s.num(core::S_SPREAD_PCT);
  t.off_confirm = (uint8_t)s.num(core::S_OFF_CONFIRM);
  t.on_fast_ms = (uint16_t)s.num(core::S_ON_FAST_MS);
  return t;
}

static void applyPin() {
  pinMode(s_pin, s_active_low ? INPUT_PULLUP : INPUT_PULLDOWN);
}

static void senseTask(void*) {
  for (;;) {
    if (s_reconfigure) {
      s_reconfigure = false;
      s_pin = (uint8_t)g_settings.num(core::S_PIN_SENSE);
      s_active_low = g_settings.flag(core::S_SENSE_ACT_LOW);
      s_force_off = g_settings.num(core::S_SENSE_MODE) == (int32_t)core::SenseMode::ForceOff;
      applyPin();
      s_classifier.begin(tuningFrom(g_settings), millis());
      s_have_off_edge = false;
    }

    const bool lit = digitalRead(s_pin) == (s_active_low ? LOW : HIGH);
    s_lit = lit;
    const uint32_t now = millis();

    const core::PcState before = s_classifier.state();
    s_classifier.sample(lit, now);
    const core::PcState after = s_classifier.state();

    if (after != before) {
      if (before == core::PcState::On && after != core::PcState::On) {
        s_off_edge_ms = now;
        s_have_off_edge = true;
      }
      if (after == core::PcState::On) s_on_edge_ms = now;
      appLogf("pc: %s -> %s (duty %u%%, spread %u%%)", core::pcStateName(before),
              core::pcStateName(after), (unsigned)s_classifier.dutyPct(),
              (unsigned)s_classifier.spreadPct());
    }

    // Jittered on purpose. A fixed sampling period can alias a PWM-dimmed LED into a constant
    // reading, and a constant DARK reading on a running PC would press its power button.
    vTaskDelay(pdMS_TO_TICKS(2 + (esp_random() % 3)));
  }
}

namespace PcSense {

void begin(const core::Settings& s) {
  s_pin = (uint8_t)s.num(core::S_PIN_SENSE);
  s_active_low = s.flag(core::S_SENSE_ACT_LOW);
  s_force_off = s.num(core::S_SENSE_MODE) == (int32_t)core::SenseMode::ForceOff;
  applyPin();
  s_classifier.begin(tuningFrom(s), millis());
  if (!s_task) {
    xTaskCreate(senseTask, "pcsense", 3072, nullptr, 3, &s_task);
  }
}

void reconfigure(const core::Settings&) { s_reconfigure = true; }

core::PcState state() {
  if (s_force_off) return core::PcState::Off;
  return s_classifier.state();
}

core::PcState rawState() { return s_classifier.rawState(); }
uint8_t dutyPct() { return s_classifier.dutyPct(); }
uint8_t spreadPct() { return s_classifier.spreadPct(); }
bool litNow() { return s_lit; }
bool ready() { return s_force_off || s_classifier.ready(); }

uint32_t msSinceOffEdge() {
  if (!s_have_off_edge) return UINT32_MAX;  // never seen on, so nothing to block after
  return millis() - s_off_edge_ms;
}

uint32_t msSinceOnEdge() {
  if (s_on_edge_ms == 0) return UINT32_MAX;
  return millis() - s_on_edge_ms;
}

}  // namespace PcSense

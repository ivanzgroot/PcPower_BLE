#include "app.h"

#include <stdarg.h>

#include "core/json_out.h"

core::Settings g_settings;
core::DeviceList g_devices;
core::RingLog g_log;

static SemaphoreHandle_t s_log_mutex = nullptr;
static uint32_t s_printed_total = 0;

void appLogBegin() {
  if (!s_log_mutex) s_log_mutex = xSemaphoreCreateMutex();
}

void appLog(const char* text) {
  if (!s_log_mutex) {
    g_log.add(millis(), text);
    return;
  }
  // A short hold: one memcpy. Never wait long enough to delay an advertisement.
  if (xSemaphoreTake(s_log_mutex, pdMS_TO_TICKS(5)) == pdTRUE) {
    g_log.add(millis(), text);
    xSemaphoreGive(s_log_mutex);
  }
}

void appLogf(const char* fmt, ...) {
  char buf[core::kLogLineLen];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof buf, fmt, ap);
  va_end(ap);
  appLog(buf);
}

void appLogPump() {
  if (!s_log_mutex) return;
  if (xSemaphoreTake(s_log_mutex, pdMS_TO_TICKS(2)) != pdTRUE) return;

  const uint32_t total = (uint32_t)g_log.total();
  const size_t held = g_log.count();
  // Lines produced since the last pump, capped by what the ring still holds.
  uint32_t behind = total - s_printed_total;
  if (behind > held) behind = held;
  for (uint32_t i = 0; i < behind; ++i) {
    const size_t index = held - behind + i;
    Serial.printf("[%8lu] %s\n", (unsigned long)g_log.timestamp(index), g_log.line(index));
  }
  s_printed_total = total;
  xSemaphoreGive(s_log_mutex);
}

void appLogClear() {
  if (s_log_mutex && xSemaphoreTake(s_log_mutex, pdMS_TO_TICKS(5)) == pdTRUE) {
    g_log.clear();
    s_printed_total = (uint32_t)g_log.total();
    xSemaphoreGive(s_log_mutex);
  }
}

size_t appLogToJson(char* buf, size_t len) {
  size_t pos = 0;
  if (s_log_mutex && xSemaphoreTake(s_log_mutex, pdMS_TO_TICKS(20)) != pdTRUE) {
    core::jsonAppend(buf, len, &pos, "{\"total\":0,\"lines\":[]}");
    return pos;
  }
  core::jsonAppend(buf, len, &pos, "{\"total\":%u,\"lines\":[", (unsigned)g_log.total());
  for (size_t i = 0; i < g_log.count(); ++i) {
    core::jsonAppend(buf, len, &pos, "%s{\"t\":%u,\"m\":\"", i ? "," : "",
                     (unsigned)g_log.timestamp(i));
    core::jsonAppendEscaped(buf, len, &pos, g_log.line(i));
    core::jsonAppend(buf, len, &pos, "\"}");
  }
  core::jsonAppend(buf, len, &pos, "]}");
  if (s_log_mutex) xSemaphoreGive(s_log_mutex);
  return pos;
}

core::TriggerConfig triggerConfigFrom(const core::Settings& s) {
  core::TriggerConfig cfg;
  cfg.postoff_block_ms = (uint32_t)s.num(core::S_POSTOFF_MS);
  cfg.cooldown_ms = (uint32_t)s.num(core::S_COOLDOWN_MS);
  cfg.require_absence = s.flag(core::S_REQUIRE_ABSENCE);
  cfg.absence_ms = (uint32_t)s.num(core::S_ABSENCE_MS);
  cfg.rssi_min = (int8_t)s.num(core::S_RSSI_MIN);
  cfg.sleep_is_off = s.flag(core::S_SLEEP_IS_OFF);
  return cfg;
}

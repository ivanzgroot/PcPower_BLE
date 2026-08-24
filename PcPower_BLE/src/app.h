// Shared state for the whole firmware, plus logging.
//
// Logging never touches Serial directly: the BLE advertisement callback is allowed to log, and
// a USB-CDC write can block for milliseconds when no host is reading. appLogPump() drains the
// ring buffer to Serial from loop() instead.
#pragma once
#include <Arduino.h>

#include "core/device_list.h"
#include "core/ringlog.h"
#include "core/settings_model.h"
#include "core/trigger.h"

inline constexpr const char* kVersion = "1.1.0";

// Set while the device list is being edited from loop() context, so the BLE host task skips it
// instead of walking a list that is being reshuffled underneath it.
extern volatile bool g_devices_locked;

extern core::Settings g_settings;
extern core::DeviceList g_devices;
extern core::RingLog g_log;

void appLogBegin();
void appLog(const char* text);         // safe from any task, including the BLE callback
void appLogf(const char* fmt, ...);    // formats first - do NOT call from the BLE fast path
void appLogPump();                     // echoes new lines to Serial; call from loop()
void appLogClear();
size_t appLogToJson(char* buf, size_t len);

// Builds the guard config from the live settings. Called from the BLE fast path, so it only
// copies scalars.
core::TriggerConfig triggerConfigFrom(const core::Settings& s);

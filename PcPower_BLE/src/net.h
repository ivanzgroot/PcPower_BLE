// WiFi: joins a stored network, and raises its own hotspot when it cannot.
//
// The hotspot is never dropped while someone might be configuring over it - a wrong password
// must not be able to lock the user out of the board.
#pragma once
#include <Arduino.h>

#include "core/settings_model.h"

namespace Net {
enum class Mode : uint8_t { Booting, Connecting, Station, Portal };

void begin(const core::Settings& s);
void tick();

Mode mode();
const char* modeName();
bool staConnected();
const char* ip();
const char* ssid();
int rssi();
bool apActive();
const char* apSsid();
const char* hostname();

bool hasCredentials();
bool saveCredentials(const char* ssid, const char* pass);
void forgetCredentials();

void startScan();                                  // asynchronous
size_t scanResultsToJson(char* buf, size_t len);   // reports progress while it runs

size_t statusToJson(char* buf, size_t len);
}  // namespace Net

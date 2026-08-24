// WiFi: joins a stored network, and raises its own hotspot when it cannot.
//
// The hotspot is never dropped while someone might be configuring over it - a wrong password
// must not be able to lock the user out of the board.
#pragma once
#include <Arduino.h>

#include "core/settings_model.h"

namespace Net {
enum class Mode : uint8_t { Off, Booting, Connecting, Station, Portal };

void begin(const core::Settings& s);  // one-time setup; leaves the radio off
void tick();

// Radio owns these: the WiFi hardware is switched off entirely while the scanner has the
// antenna, and brought back when the PC comes up.
void resume();
void shutdown();
bool enabled();

// WiFi power save. Worth having when BLE is sharing the antenna, actively harmful during a
// sustained transfer: the modem naps between beacons and a large upload stalls and drops.
void setPowerSave(bool enabled);

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

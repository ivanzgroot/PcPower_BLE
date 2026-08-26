// A flight recorder for problems that only show up after long uptime and leave no trace once
// the board is power-cycled.
//
// The log is RAM-only and dies with the power. If something degrades slowly over a day and the
// only way back in is pulling the board to plug it into a laptop - which cuts power - every
// symptom is gone by the time anyone can look. This module keeps a small, cheap snapshot in NVS
// so the NEXT boot can say what the LAST session looked like just before it ended: how long it
// ran, how tight memory got, and how often the BLE watchdog had to intervene.
#pragma once
#include <Arduino.h>

namespace Diag {
void begin();  // logs the previous session's snapshot, then starts a fresh one
void tick();   // periodically persists the current session's snapshot

void noteBleSoftRestart();  // the scan was restarted; nothing heard in 30s
void noteBleHardRestart();  // the whole BLE stack was reinitialised
void noteApStartFailure();  // WiFi.softAP() returned false

uint32_t minFreeHeapBytes();      // lowest free heap since boot
size_t largestFreeBlockBytes();   // largest single allocation the heap could satisfy right now

size_t toJson(char* buf, size_t len);
}  // namespace Diag

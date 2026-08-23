// The wake path: listen for a known controller advertising, and press the power button.
//
// The guards are evaluated inside the advertisement callback so the pulse starts within
// milliseconds of the radio hearing the controller. Nothing on that path may allocate, log
// through printf, touch NVS or take a long lock.
#pragma once
#include <Arduino.h>

#include "core/learner.h"
#include "core/settings_model.h"
#include "core/trigger.h"

namespace BleScan {
void begin(const core::Settings& s);
void reconfigure(const core::Settings& s);
void tick();  // watchdog and deferred logging; call from loop()

void setPaused(bool paused);     // stops the radio, e.g. while the PC is running
bool paused();
bool scanning();
void setInhibited(bool inhibit);  // blocks pulses without stopping the scan (OTA, learning)
bool inhibited();

void startLearning(uint32_t duration_ms);
void cancelLearning();
bool learning();
core::Learner& learner();

uint32_t advertsSeen();
uint32_t msSinceLastAdvert();
core::TriggerReason lastReason();
uint32_t msSinceLastReason();
}  // namespace BleScan

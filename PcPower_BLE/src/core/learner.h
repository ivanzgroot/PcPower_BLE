// Learning mode: hold the controller close, switch it on, and the strongest advertiser in a
// five-second window wins.
//
// Candidates with rotating addresses are still collected, so the UI can show the user WHY their
// controller was refused instead of silently ignoring it.
#pragma once
#include <cstddef>
#include <cstdint>

#include "core/ble_addr.h"

namespace core {

static constexpr uint8_t kMaxCandidates = 32;
static constexpr size_t kCandidateNameLen = 24;

struct LearnCandidate {
  uint8_t addr[6] = {0};
  uint8_t addr_type = 0;
  AddrKind kind = AddrKind::PublicStatic;
  int8_t best_rssi = -128;
  uint16_t hits = 0;
  char name[kCandidateNameLen] = {0};
};

class Learner {
 public:
  void start(uint32_t now_ms, uint32_t duration_ms);
  void cancel();
  bool active(uint32_t now_ms) const;
  uint32_t remaining(uint32_t now_ms) const;

  void feed(const uint8_t addr[6], uint8_t addr_type, int8_t rssi, const char* name);

  uint8_t count() const { return count_; }
  const LearnCandidate& at(uint8_t i) const { return candidates_[i]; }  // strongest first
  int bestStable() const;  // index of the strongest learnable candidate, -1 if none

  size_t toJson(char* buf, size_t len, uint32_t now_ms) const;

 private:
  LearnCandidate candidates_[kMaxCandidates];
  uint8_t count_ = 0;
  uint32_t start_ms_ = 0;
  uint32_t duration_ms_ = 0;
  bool running_ = false;
};

}  // namespace core

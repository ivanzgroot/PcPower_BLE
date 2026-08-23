#include "learner.h"

#include <cstdio>
#include <cstring>

#include "json_out.h"

namespace core {

void Learner::start(uint32_t now_ms, uint32_t duration_ms) {
  count_ = 0;
  start_ms_ = now_ms;
  duration_ms_ = duration_ms;
  running_ = true;
}

void Learner::cancel() { running_ = false; }

bool Learner::active(uint32_t now_ms) const {
  return running_ && (uint32_t)(now_ms - start_ms_) < duration_ms_;
}

uint32_t Learner::remaining(uint32_t now_ms) const {
  if (!active(now_ms)) return 0;
  return duration_ms_ - (uint32_t)(now_ms - start_ms_);
}

void Learner::feed(const uint8_t addr[6], uint8_t addr_type, int8_t rssi, const char* name) {
  if (!running_) return;

  for (uint8_t i = 0; i < count_; ++i) {
    LearnCandidate& c = candidates_[i];
    if (c.addr_type != addr_type || !addrEqual(c.addr, addr)) continue;
    ++c.hits;
    if (rssi > c.best_rssi) c.best_rssi = rssi;
    // A name usually arrives in the scan response, not the advertisement, so keep the first
    // real one we see rather than letting a later empty one wipe it.
    if (c.name[0] == '\0' && name && name[0]) {
      std::snprintf(c.name, kCandidateNameLen, "%s", name);
    }
    // Keep the list sorted strongest-first.
    while (i > 0 && candidates_[i - 1].best_rssi < candidates_[i].best_rssi) {
      const LearnCandidate tmp = candidates_[i - 1];
      candidates_[i - 1] = candidates_[i];
      candidates_[i] = tmp;
      --i;
    }
    return;
  }

  LearnCandidate fresh;
  std::memcpy(fresh.addr, addr, 6);
  fresh.addr_type = addr_type;
  fresh.kind = classifyAddress(addr, addr_type);
  fresh.best_rssi = rssi;
  fresh.hits = 1;
  std::snprintf(fresh.name, kCandidateNameLen, "%s", name ? name : "");

  if (count_ < kMaxCandidates) {
    candidates_[count_++] = fresh;
  } else if (rssi > candidates_[count_ - 1].best_rssi) {
    candidates_[count_ - 1] = fresh;  // displace the weakest
  } else {
    return;
  }

  for (uint8_t i = (uint8_t)(count_ - 1);
       i > 0 && candidates_[i - 1].best_rssi < candidates_[i].best_rssi; --i) {
    const LearnCandidate tmp = candidates_[i - 1];
    candidates_[i - 1] = candidates_[i];
    candidates_[i] = tmp;
  }
}

int Learner::bestStable() const {
  for (uint8_t i = 0; i < count_; ++i) {
    if (isStable(candidates_[i].kind)) return i;
  }
  return -1;
}

size_t Learner::toJson(char* buf, size_t len, uint32_t now_ms) const {
  size_t pos = 0;
  jsonAppend(buf, len, &pos, "{\"active\":%s,\"remaining_ms\":%u,\"best_stable\":%d,\"candidates\":[",
             active(now_ms) ? "true" : "false", (unsigned)remaining(now_ms), bestStable());
  for (uint8_t i = 0; i < count_; ++i) {
    const LearnCandidate& c = candidates_[i];
    char addr[18];
    formatAddress(c.addr, addr, sizeof addr);
    jsonAppend(buf, len, &pos, "%s{\"index\":%u,\"addr\":\"%s\",\"type\":%u,\"name\":\"", i ? "," : "",
               (unsigned)i, addr, (unsigned)c.addr_type);
    jsonAppendEscaped(buf, len, &pos, c.name);
    jsonAppend(buf, len, &pos, "\",\"rssi\":%d,\"hits\":%u,\"kind\":\"%s\",\"learnable\":%s,\"reason\":\"",
               (int)c.best_rssi, (unsigned)c.hits, addrKindName(c.kind),
               isStable(c.kind) ? "true" : "false");
    jsonAppendEscaped(buf, len, &pos, addrKindReason(c.kind));
    jsonAppend(buf, len, &pos, "\"}");
  }
  jsonAppend(buf, len, &pos, "]}");
  return pos;
}

}  // namespace core

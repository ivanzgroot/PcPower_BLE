#include "ringlog.h"

#include <cstdio>

namespace core {

void RingLog::add(uint32_t ms, const char* text) {
  // snprintf truncates safely and always terminates.
  std::snprintf(lines_[head_], kLogLineLen, "%s", text ? text : "");
  stamps_[head_] = ms;
  head_ = (head_ + 1) % kLogLines;
  if (count_ < kLogLines) ++count_;
  ++total_;
}

size_t RingLog::indexOf(size_t i) const {
  return (head_ + kLogLines - count_ + i) % kLogLines;
}

const char* RingLog::line(size_t i) const {
  if (i >= count_) return "";
  return lines_[indexOf(i)];
}

uint32_t RingLog::timestamp(size_t i) const {
  if (i >= count_) return 0;
  return stamps_[indexOf(i)];
}

void RingLog::clear() {
  head_ = 0;
  count_ = 0;
}

}  // namespace core

// A fixed-size log of the most recent lines, with millisecond timestamps.
// Lives in core/ so it can be unit-tested natively: no Arduino, no allocation.
#pragma once
#include <cstddef>
#include <cstdint>

namespace core {

static constexpr size_t kLogLines = 64;
static constexpr size_t kLogLineLen = 96;  // including the terminator

class RingLog {
 public:
  // Copies `text` (truncating to kLogLineLen - 1 characters). A null pointer logs an empty line.
  void add(uint32_t ms, const char* text);

  size_t count() const { return count_; }      // lines currently held
  size_t total() const { return total_; }      // lines ever added, including dropped ones
  const char* line(size_t i) const;            // i == 0 is the oldest line still held
  uint32_t timestamp(size_t i) const;
  void clear();

 private:
  size_t indexOf(size_t i) const;

  char lines_[kLogLines][kLogLineLen] = {};
  uint32_t stamps_[kLogLines] = {};
  size_t head_ = 0;    // where the next line goes
  size_t count_ = 0;
  uint32_t total_ = 0;
};

}  // namespace core

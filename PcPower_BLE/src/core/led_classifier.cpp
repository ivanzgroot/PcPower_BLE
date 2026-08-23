#include "core/led_classifier.h"

namespace core {

const char* pcStateName(PcState s) {
  switch (s) {
    case PcState::Unknown: return "unknown";
    case PcState::Off: return "off";
    case PcState::Sleep: return "sleep";
    case PcState::On: return "on";
  }
  return "unknown";
}

void LedClassifier::begin(const LedTuning& tuning, uint32_t now_ms) {
  tuning_ = tuning;
  if (tuning_.blocks < 2) tuning_.blocks = 2;
  if (tuning_.blocks > kMaxBlocks) tuning_.blocks = kMaxBlocks;
  block_ms_ = tuning_.window_ms / tuning_.blocks;
  if (block_ms_ < 10) block_ms_ = 10;

  block_head_ = 0;
  block_count_ = 0;
  for (uint8_t i = 0; i < kMaxBlocks; ++i) { block_duty_[i] = 0; block_edges_[i] = 0; }
  edge_acc_ = 0;
  lit_acc_ms_ = 0;
  total_acc_ms_ = 0;
  block_start_ms_ = now_ms;
  last_sample_ms_ = now_ms;
  started_ = false;
  currently_lit_ = false;
  lit_since_ms_ = 0;
  state_ = PcState::Unknown;
  raw_ = PcState::Unknown;
  duty_pct_ = 0;
  spread_pct_ = 0;
  off_streak_ = 0;
  ready_ = false;
  last_change_ms_ = now_ms;
}

void LedClassifier::sample(bool lit, uint32_t now_ms) {
  if (!started_) {
    started_ = true;
    last_sample_ms_ = now_ms;
    block_start_ms_ = now_ms;
  }

  uint32_t dt = now_ms - last_sample_ms_;
  if (dt > block_ms_) dt = block_ms_;  // a stalled sampler must not swamp the block
  last_sample_ms_ = now_ms;

  total_acc_ms_ += dt;
  if (lit) lit_acc_ms_ += dt;

  // Fast path into ON: unbroken light for on_fast_ms is believed at once, because being slow to
  // notice the PC came up is the cheap mistake and being slow to notice it went down is not.
  //
  // It stands down when the LED has gone dark more than once inside the window. A 1 Hz sleep
  // blink holds the LED lit for 500 ms every second, which would otherwise re-enter ON forever
  // and make a sleeping PC impossible to wake. A PC actually powering on goes dark-to-lit once
  // and stays there, so it keeps the fast path.
  if (lit) {
    if (!currently_lit_) {
      currently_lit_ = true;
      lit_since_ms_ = now_ms;
    } else if (state_ != PcState::On && fallingEdges() < 2 &&
               (now_ms - lit_since_ms_) >= tuning_.on_fast_ms) {
      setState(PcState::On, now_ms);
      off_streak_ = 0;
    }
  } else {
    if (currently_lit_ && edge_acc_ < 0xFFFF) ++edge_acc_;
    currently_lit_ = false;
  }

  if (now_ms - block_start_ms_ >= block_ms_) closeBlock(now_ms);
}

void LedClassifier::closeBlock(uint32_t now_ms) {
  uint8_t duty = currently_lit_ ? 100 : 0;
  if (total_acc_ms_ > 0) duty = (uint8_t)((lit_acc_ms_ * 100) / total_acc_ms_);

  block_duty_[block_head_] = duty;
  block_edges_[block_head_] = edge_acc_ > 255 ? 255 : (uint8_t)edge_acc_;
  block_head_ = (uint8_t)((block_head_ + 1) % tuning_.blocks);
  if (block_count_ < tuning_.blocks) ++block_count_;

  edge_acc_ = 0;
  lit_acc_ms_ = 0;
  total_acc_ms_ = 0;
  block_start_ms_ = now_ms;  // resync after a stall rather than replaying missed blocks

  if (block_count_ >= tuning_.blocks) classify(now_ms);
}

void LedClassifier::classify(uint32_t now_ms) {
  uint32_t sum = 0;
  uint8_t lo = 100, hi = 0;
  for (uint8_t i = 0; i < tuning_.blocks; ++i) {
    const uint8_t d = block_duty_[i];
    sum += d;
    if (d < lo) lo = d;
    if (d > hi) hi = d;
  }
  duty_pct_ = (uint8_t)(sum / tuning_.blocks);
  spread_pct_ = (uint8_t)(hi - lo);
  ready_ = true;

  if (duty_pct_ >= tuning_.on_duty_pct) {
    raw_ = PcState::On;
  } else if (duty_pct_ <= tuning_.off_duty_pct) {
    raw_ = PcState::Off;
  } else if (spread_pct_ < tuning_.spread_pct) {
    raw_ = PcState::On;  // steady brightness: a dimmed LED, not a pulsing one
  } else {
    raw_ = PcState::Sleep;  // brightness varies across the window: blinking or breathing
  }

  if (raw_ == PcState::On) {
    setState(PcState::On, now_ms);
    off_streak_ = 0;
    return;
  }

  if (state_ == PcState::On) {
    // Leaving ON is the dangerous direction. Require sustained disagreement - long enough that
    // a brief dropout, whose tail lingers in the window for a further window_ms, cannot do it.
    if (++off_streak_ >= tuning_.off_confirm) {
      setState(raw_, now_ms);
      off_streak_ = 0;
    }
    return;
  }

  setState(raw_, now_ms);  // off <-> sleep switch freely, and so does unknown -> either
  off_streak_ = 0;
}

uint16_t LedClassifier::fallingEdges() const {
  uint16_t sum = edge_acc_;
  for (uint8_t i = 0; i < tuning_.blocks; ++i) sum = (uint16_t)(sum + block_edges_[i]);
  return sum;
}

void LedClassifier::setState(PcState s, uint32_t now_ms) {
  if (s == state_) return;
  state_ = s;
  last_change_ms_ = now_ms;
}

}  // namespace core

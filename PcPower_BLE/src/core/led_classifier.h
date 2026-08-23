// Turns a stream of PWR-LED samples into on / off / sleep.
//
// The asymmetry here is deliberate and load-bearing: a false "on" costs a delayed wake, while a
// false "off" presses the power button on a running PC. So entering ON is immediate and leaving
// ON is slow and needs repeated confirmation.
//
// Samples arrive at irregular intervals on purpose - the sampler jitters its period so a
// PWM-dimmed LED cannot alias into a constant reading - so each sample is weighted by the time
// since the previous one rather than counted.
#pragma once
#include <cstdint>

namespace core {

enum class PcState : uint8_t { Unknown, Off, Sleep, On };
const char* pcStateName(PcState s);

struct LedTuning {
  uint16_t window_ms = 2000;  // how much history each decision looks at
  uint8_t blocks = 8;         // the window is split into this many sub-blocks
  uint8_t on_duty_pct = 95;   // lit at least this much of the window -> on
  uint8_t off_duty_pct = 2;   // lit at most this much -> off
  uint8_t spread_pct = 35;    // between the two: steadier than this -> dimmed (on), else sleep
  uint8_t off_confirm = 12;   // consecutive non-on classifications needed to leave on (~3 s)
  uint16_t on_fast_ms = 300;  // unbroken light for this long enters on at once
};

static constexpr uint8_t kMaxBlocks = 16;

class LedClassifier {
 public:
  void begin(const LedTuning& tuning, uint32_t now_ms);
  void sample(bool lit, uint32_t now_ms);

  PcState state() const { return state_; }        // hysteresis applied - what the app uses
  PcState rawState() const { return raw_; }       // last window classification, no hysteresis
  uint8_t dutyPct() const { return duty_pct_; }   // last completed window
  uint8_t spreadPct() const { return spread_pct_; }
  bool ready() const { return ready_; }
  uint32_t lastChangeMs() const { return last_change_ms_; }
  const LedTuning& tuning() const { return tuning_; }

 private:
  void closeBlock(uint32_t now_ms);
  void classify(uint32_t now_ms);
  void setState(PcState s, uint32_t now_ms);
  uint16_t fallingEdges() const;  // lit -> dark transitions held in the window

  LedTuning tuning_;
  uint32_t block_ms_ = 250;

  uint8_t block_duty_[kMaxBlocks] = {};
  uint8_t block_edges_[kMaxBlocks] = {};
  uint8_t block_head_ = 0;
  uint8_t block_count_ = 0;

  uint32_t block_start_ms_ = 0;
  uint32_t lit_acc_ms_ = 0;
  uint32_t total_acc_ms_ = 0;
  uint32_t last_sample_ms_ = 0;
  bool started_ = false;

  bool currently_lit_ = false;
  uint32_t lit_since_ms_ = 0;
  uint16_t edge_acc_ = 0;

  PcState state_ = PcState::Unknown;
  PcState raw_ = PcState::Unknown;
  uint8_t duty_pct_ = 0;
  uint8_t spread_pct_ = 0;
  uint8_t off_streak_ = 0;
  bool ready_ = false;
  uint32_t last_change_ms_ = 0;
};

}  // namespace core

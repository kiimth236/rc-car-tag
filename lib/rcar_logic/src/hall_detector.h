#pragma once
#include <stdint.h>

namespace rcar {

struct HallConfig {
  float thresholdMv;   // |값 − 기준| 이 이 이상이면 "가까움"
  int confirmSamples;  // 연속 몇 번이면 잡힘 (10ms 간격 × 5 = 50ms)
  float driftAlpha;    // 대기 중 기준값이 따라가는 비율
};

constexpr HallConfig kDefaultHallConfig{30.0f, 5, 0.01f};

class HallDetector {
 public:
  explicit HallDetector(const HallConfig& cfg = kDefaultHallConfig) : cfg_(cfg) {}
  void setBaseline(float mv) { baseline_ = mv; }
  void setThreshold(float mv) { cfg_.thresholdMv = mv; }
  // armed = 게임 중이고 아직 안 잡힘. 잡힌 순간에만 true
  bool update(float mv, bool armed);
  void reset() { count_ = 0; fired_ = false; }
  float delta() const { return delta_; }
  float baseline() const { return baseline_; }
  float threshold() const { return cfg_.thresholdMv; }

 private:
  HallConfig cfg_;
  float baseline_ = 0.0f;
  float delta_ = 0.0f;
  int count_ = 0;
  bool fired_ = false;
};

}  // namespace rcar

#pragma once
#include <stdint.h>
#include "rcar_types.h"

namespace rcar {

struct BatteryConfig {
  float warnV;            // 이 아래 = 경고
  float clearV;           // 경고가 풀리는 전압
  float cutoffV;          // 이 아래가 cutoffHoldMs 계속 = 정지 고정
  float noBatteryV;       // 이 아래 = 배터리 없음 (USB만)
  uint32_t cutoffHoldMs;
  float alpha;            // 이동평균 비율 (100ms마다 0.05 ≈ 2초)
};

constexpr BatteryConfig kDefaultBatteryConfig{6.8f, 7.0f, 6.4f, 3.0f, 3000, 0.05f};

class BatteryMonitor {
 public:
  explicit BatteryMonitor(const BatteryConfig& cfg = kDefaultBatteryConfig) : cfg_(cfg) {}
  void update(float volts, uint32_t nowMs);
  float filtered() const { return filtered_; }
  BatteryState state() const { return state_; }

 private:
  BatteryConfig cfg_;
  float filtered_ = 0.0f;
  bool hasSample_ = false;
  bool belowActive_ = false;
  uint32_t belowSince_ = 0;
  bool latched_ = false;
  BatteryState state_ = BatteryState::NoBattery;
};

}  // namespace rcar

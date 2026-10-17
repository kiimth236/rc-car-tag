#include "battery_monitor.h"

namespace rcar {

void BatteryMonitor::update(float volts, uint32_t nowMs) {
  if (!hasSample_) {
    filtered_ = volts;
    hasSample_ = true;
  } else {
    filtered_ += cfg_.alpha * (volts - filtered_);
  }
  if (latched_) { state_ = BatteryState::Cutoff; return; }
  if (filtered_ < cfg_.noBatteryV) {  // USB만 연결 · 스위치 OFF
    belowActive_ = false;
    state_ = BatteryState::NoBattery;
    return;
  }
  if (filtered_ < cfg_.cutoffV) {
    if (!belowActive_) { belowActive_ = true; belowSince_ = nowMs; }
    if (nowMs - belowSince_ >= cfg_.cutoffHoldMs) {
      latched_ = true;
      state_ = BatteryState::Cutoff;
      return;
    }
    state_ = BatteryState::Warn;
    return;
  }
  belowActive_ = false;
  bool stayWarn = (state_ == BatteryState::Warn) && filtered_ < cfg_.clearV;
  state_ = (filtered_ < cfg_.warnV || stayWarn) ? BatteryState::Warn : BatteryState::Ok;
}

}  // namespace rcar

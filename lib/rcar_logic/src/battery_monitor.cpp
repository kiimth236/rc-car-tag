#include "battery_monitor.h"

namespace rcar {

void BatteryMonitor::update(float volts, uint32_t nowMs) {
  bool switchedOn = state_ == BatteryState::NoBattery && volts >= cfg_.noBatteryV;
  if (!hasSample_ || switchedOn) {
    filtered_ = volts;  // 첫 값 · 배터리 스위치를 막 켰을 때는 평균이 0V 에서 천천히 올라오지 않게 바로 맞춤
    hasSample_ = true;
  } else {
    filtered_ += cfg_.alpha * (volts - filtered_);
  }
  if (filtered_ < cfg_.noBatteryV) {  // USB만 연결 · 스위치 OFF → 정지 고정도 풀림 (스위치를 껐다 켜기)
    belowActive_ = false;
    latched_ = false;
    state_ = BatteryState::NoBattery;
    return;
  }
  if (latched_) { state_ = BatteryState::Cutoff; return; }
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

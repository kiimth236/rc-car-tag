#include "drive.h"
#include "drive_math.h"
#include "rcar_config.h"

void Drive::brakeNow(uint32_t now) {
  l_.brake();
  r_.brake();
  curL_ = curR_ = 0;
  braking_ = true;
  brakeUntil_ = now + cfg::BRAKE_HOLD_MS;
}

void Drive::update(uint32_t now, float vbat, float distCm, bool enabled) {
  if (braking_) {
    if ((int32_t)(now - brakeUntil_) < 0) return;
    braking_ = false;
    l_.coast();
    r_.coast();
  }
  float thr = enabled ? rcar::limitForward(thr_, distCm, cfg::SLOW_CM, cfg::STOP_CM, cfg::SLOW_FACTOR) : 0.0f;
  float st = enabled ? steer_ : 0.0f;
  float lf, rf;
  rcar::mixTank(thr, st, lf, rf);
  int maxD = rcar::maxDutyForBattery(vbat, cfg::V_TARGET, cfg::V_DROP);
  curL_ = rcar::rampStep(curL_, rcar::commandToDuty(lf, minL_, maxD), cfg::RAMP_STEP);
  curR_ = rcar::rampStep(curR_, rcar::commandToDuty(rf, minR_, maxD), cfg::RAMP_STEP);
  l_.set(curL_);
  r_.set(curR_);
}

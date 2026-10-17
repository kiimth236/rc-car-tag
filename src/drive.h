#pragma once
#include <stdint.h>
#include "motor_l298n.h"

// 조종 입력(-1~1) → 두 모터. 배터리 보정 · 최소 듀티 · 부드러운 가속 · 전진 제한 · 급정지
class Drive {
 public:
  Drive(L298NMotor& l, L298NMotor& r) : l_(l), r_(r) {}
  void setMinDuty(int l, int r) { minL_ = l; minR_ = r; }
  void command(float throttle, float steer) { thr_ = throttle; steer_ = steer; }
  // 10ms 마다. distCm < 0 = 전진 제한 없음
  void update(uint32_t now, float vbat, float distCm, bool enabled);
  void brakeNow(uint32_t now);

 private:
  L298NMotor& l_;
  L298NMotor& r_;
  int minL_ = 90, minR_ = 90;
  float thr_ = 0, steer_ = 0;
  int curL_ = 0, curR_ = 0;
  bool braking_ = false;
  uint32_t brakeUntil_ = 0;
};

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
  int dutyL() const { return curL_; }  // 지금 나가는 듀티 (−255~255, 부호 = 방향). 정지·급정지 0
  int dutyR() const { return curR_; }
  bool frontBlocked() const { return blocked_; }  // 마지막 update: 앞으로 가라는데 앞 거리 제한으로 0

 private:
  L298NMotor& l_;
  L298NMotor& r_;
  int minL_ = 90, minR_ = 90;
  float thr_ = 0, steer_ = 0;
  int curL_ = 0, curR_ = 0;
  bool braking_ = false;
  bool blocked_ = false;
  uint32_t brakeUntil_ = 0;
};

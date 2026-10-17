#pragma once
#include <stdint.h>

// L298N 모터 1개. duty: -255 ~ 255 (+ = 차 앞쪽), 0 = 힘 빼고 굴러감(coast)
class L298NMotor {
 public:
  L298NMotor(uint8_t en, uint8_t in1, uint8_t in2, uint8_t ch) : en_(en), in1_(in1), in2_(in2), ch_(ch) {}
  void begin(uint32_t pwmHz);
  void setInvert(bool v) { invert_ = v; }
  bool inverted() const { return invert_; }
  void set(int duty);
  void brake();  // IN1 = IN2 = HIGH, EN = 255 → 급정지
  void coast();  // EN = 0
  int duty() const { return duty_; }

 private:
  uint8_t en_, in1_, in2_, ch_;
  bool invert_ = false;
  int duty_ = 0;
};

#include "motor_l298n.h"
#include <Arduino.h>
#include "rcar_config.h"

void L298NMotor::begin(uint32_t pwmHz) {
  pinMode(in1_, OUTPUT);
  pinMode(in2_, OUTPUT);
  digitalWrite(in1_, LOW);
  digitalWrite(in2_, LOW);
  ledcSetup(ch_, pwmHz, cfg::PWM_BITS);  // 코어 2.0.17 방식
  ledcAttachPin(en_, ch_);
  ledcWrite(ch_, 0);
  duty_ = 0;
}

void L298NMotor::set(int duty) {
  if (duty > 255) duty = 255;
  if (duty < -255) duty = -255;
  duty_ = duty;
  if (duty == 0) { coast(); return; }
  bool forward = duty > 0;
  if (invert_) forward = !forward;
  digitalWrite(in1_, forward ? HIGH : LOW);
  digitalWrite(in2_, forward ? LOW : HIGH);
  ledcWrite(ch_, (uint32_t)abs(duty));
}

void L298NMotor::brake() {
  duty_ = 0;
  digitalWrite(in1_, HIGH);
  digitalWrite(in2_, HIGH);
  ledcWrite(ch_, 255);
}

void L298NMotor::coast() {
  duty_ = 0;
  ledcWrite(ch_, 0);
  digitalWrite(in1_, LOW);
  digitalWrite(in2_, LOW);
}

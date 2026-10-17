#include "sonar.h"
#include <Arduino.h>
#include "rcar_config.h"

void sonarBegin() {
  pinMode(cfg::PIN_TRIG, OUTPUT);
  digitalWrite(cfg::PIN_TRIG, LOW);
  pinMode(cfg::PIN_ECHO, INPUT);
}

float sonarReadCm() {
  digitalWrite(cfg::PIN_TRIG, LOW);
  delayMicroseconds(4);
  digitalWrite(cfg::PIN_TRIG, HIGH);
  delayMicroseconds(10);
  digitalWrite(cfg::PIN_TRIG, LOW);
  uint32_t us = pulseIn(cfg::PIN_ECHO, HIGH, cfg::ECHO_TIMEOUT_US);
  if (us == 0) return -1.0f;
  return us / 58.0f;
}

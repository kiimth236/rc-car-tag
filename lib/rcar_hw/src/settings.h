#pragma once
#include <stdint.h>
#include "rcar_types.h"

// 차마다 다른 설정 (NVS "rcar"). carId 0 = 아직 안 정함
struct CarSettings {
  uint8_t carId;
  rcar::Role role;
  bool invertL, invertR;
  int minDutyL, minDutyR;
  float vbatK;
  float hallThresholdMv;
  uint32_t pwmHz;
};

CarSettings settingsDefaults();
CarSettings settingsLoad();  // "rcar" 에 없는 값은 예전 점검 프로그램("car1") 값 → 기본값 순서로
void settingsSave(const CarSettings& s);
void settingsClear();

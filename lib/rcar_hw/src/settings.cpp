#include "settings.h"
#include <Preferences.h>
#include "rcar_config.h"

CarSettings settingsDefaults() {
  CarSettings s;
  s.carId = 0;
  s.role = rcar::Role::Runner;
  s.invertL = s.invertR = false;
  s.minDutyL = s.minDutyR = cfg::MIN_DUTY_DEFAULT;
  s.vbatK = cfg::VBAT_K_DEFAULT;
  s.hallThresholdMv = cfg::HALL_THRESHOLD_MV_DEFAULT;
  s.pwmHz = cfg::MOTOR_PWM_HZ_DEFAULT;
  return s;
}

CarSettings settingsLoad() {
  CarSettings s = settingsDefaults();
  Preferences old;  // v1.x 점검 프로그램이 저장한 값 (있으면 가져옴)
  if (old.begin("car1", true)) {
    s.vbatK = old.getFloat("k", s.vbatK);
    s.invertL = old.getBool("invL", s.invertL);
    s.invertR = old.getBool("invR", s.invertR);
    int ml = old.getInt("minL", 0), mr = old.getInt("minR", 0);
    if (ml > 0) s.minDutyL = ml;
    if (mr > 0) s.minDutyR = mr;
    old.end();
  }
  Preferences p;
  p.begin("rcar", true);
  s.carId = p.getUChar("id", s.carId);
  s.role = (rcar::Role)p.getUChar("role", (uint8_t)s.role);
  s.invertL = p.getBool("invL", s.invertL);
  s.invertR = p.getBool("invR", s.invertR);
  s.minDutyL = p.getInt("minL", s.minDutyL);
  s.minDutyR = p.getInt("minR", s.minDutyR);
  s.vbatK = p.getFloat("k", s.vbatK);
  s.hallThresholdMv = p.getFloat("hallTh", s.hallThresholdMv);
  s.pwmHz = p.getUInt("pwmHz", s.pwmHz);
  p.end();
  if (s.carId > 4) s.carId = 0;
  if ((uint8_t)s.role > 1) s.role = rcar::Role::Runner;
  return s;
}

void settingsSave(const CarSettings& s) {
  Preferences p;
  p.begin("rcar", false);
  p.putUChar("id", s.carId);
  p.putUChar("role", (uint8_t)s.role);
  p.putBool("invL", s.invertL);
  p.putBool("invR", s.invertR);
  p.putInt("minL", s.minDutyL);
  p.putInt("minR", s.minDutyR);
  p.putFloat("k", s.vbatK);
  p.putFloat("hallTh", s.hallThresholdMv);
  p.putUInt("pwmHz", s.pwmHz);
  p.end();
}

void settingsClear() {
  Preferences p;
  p.begin("rcar", false);
  p.clear();
  p.end();
  Preferences old;
  old.begin("car1", false);
  old.clear();
  old.end();
}

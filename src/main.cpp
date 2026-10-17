/*
 * RC카 본 프로그램 — 1단계: Dabble 조종 주행 (게임 · 무선은 Task 10)
 * 차 번호 · 역할 · 보정값은 점검 프로그램(car1_test)의 r · m · n · c · k 로 먼저 저장하세요.
 */
#include <Arduino.h>
#include <esp_system.h>
#include "analog_inputs.h"
#include "battery_monitor.h"
#include "ble_control.h"
#include "drive.h"
#include "drive_math.h"
#include "led_patterns.h"
#include "motor_l298n.h"
#include "rcar_config.h"
#include "rgb_led.h"
#include "settings.h"
#include "sonar.h"

static CarSettings S;
static L298NMotor motorL(cfg::PIN_ENA, cfg::PIN_IN1, cfg::PIN_IN2, cfg::CH_MOTOR_L);
static L298NMotor motorR(cfg::PIN_ENB, cfg::PIN_IN3, cfg::PIN_IN4, cfg::CH_MOTOR_R);
static Drive drive(motorL, motorR);
static rcar::BatteryMonitor battery;
static float sonarHist[3] = {-1, -1, -1};
static uint8_t sonarIdx = 0;
static float frontCm = -1;
static uint32_t nextControl = 0, nextSonar = 0, nextBattery = 0, nextLed = 0, nextLog = 0;

static bool due(uint32_t now, uint32_t& next, uint32_t period) {
  if ((int32_t)(now - next) < 0) return false;
  next = now + period;
  return true;
}

static void blinkBoot(rcar::Rgb c, int times) {
  for (int i = 0; i < times; i++) { ledWrite(c); delay(150); ledWrite({0, 0, 0}); delay(150); }
}

void setup() {
  motorL.begin(cfg::MOTOR_PWM_HZ_DEFAULT);  // 가장 먼저 모터 정지 상태로
  motorR.begin(cfg::MOTOR_PWM_HZ_DEFAULT);
  Serial.begin(115200);
  ledBegin();
  analogInputsBegin();
  sonarBegin();
  S = settingsLoad();
  motorL.begin(S.pwmHz);
  motorR.begin(S.pwmHz);
  motorL.setInvert(S.invertL);
  motorR.setInvert(S.invertR);
  drive.setMinDuty(S.minDutyL, S.minDutyR);

  esp_reset_reason_t rr = esp_reset_reason();
  Serial.printf("\n[RC카] 차 %d번 %s · 직전 리셋 원인 %d%s\n", S.carId,
                S.role == rcar::Role::Chaser ? "추격" : "도주", (int)rr,
                rr == ESP_RST_BROWNOUT ? " (전압 부족! 배선·커패시터·배터리 확인)" : "");
  if (rr == ESP_RST_BROWNOUT) blinkBoot({cfg::LED_LEVEL, 0, cfg::LED_LEVEL}, 3);  // 보라 3번
  if (S.carId == 0) Serial.println("[주의] 차 번호가 없습니다. car1_test 의 r 명령으로 정하세요 (지금은 주행만).");

  char name[16];
  snprintf(name, sizeof name, "RCAR-%u", S.carId);
  bleBegin(name);
  Serial.printf("BLE 이름: %s\n", name);
}

void loop() {
  uint32_t now = millis();
  PadInput pad = blePoll();

  if (due(now, nextBattery, 100)) battery.update(batteryPinVolts() * S.vbatK, now);

  if (S.role == rcar::Role::Chaser && due(now, nextSonar, cfg::SONAR_PERIOD_MS)) {
    sonarHist[sonarIdx] = sonarReadCm();
    sonarIdx = (sonarIdx + 1) % 3;
    frontCm = rcar::median3Distance(sonarHist[0], sonarHist[1], sonarHist[2]);
  }

  if (due(now, nextControl, cfg::CONTROL_PERIOD_MS)) {
    bool enabled = pad.connected && battery.state() != rcar::BatteryState::Cutoff;
    drive.command(pad.throttle, pad.steer);
    drive.update(now, battery.filtered(), S.role == rcar::Role::Chaser ? frontCm : -1.0f, enabled);
  }

  if (due(now, nextLed, 20)) {
    rcar::LedPattern p = pad.connected ? rcar::LedPattern::RoleSolid : rcar::LedPattern::IdleBlink;
    ledWrite(rcar::patternColor(p, S.role, S.carId, 0, battery.state(), now, cfg::LED_LEVEL));
  }

  if (due(now, nextLog, 1000)) {
    Serial.printf("BLE %s · 배터리 %.2fV (%d) · 앞 %.1fcm · 입력 %.2f/%.2f\n", pad.connected ? "연결" : "대기",
                  battery.filtered(), (int)battery.state(), frontCm, pad.throttle, pad.steer);
  }
}

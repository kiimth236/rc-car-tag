#pragma once
#include <stdint.h>

// 핀과 설정 상수 — 배선 가이드(rc-car-l298n-wiring.html) 전체 연결표와 같아야 합니다.
namespace cfg {
// L298N
constexpr uint8_t PIN_ENA = 25, PIN_IN1 = 26, PIN_IN2 = 27;  // 왼쪽 모터 (ENA = PWM)
constexpr uint8_t PIN_ENB = 13, PIN_IN3 = 32, PIN_IN4 = 33;  // 오른쪽 모터 (ENB = PWM)
// 센서 · LED
constexpr uint8_t PIN_TRIG = 18, PIN_ECHO = 19;             // 초음파 (Echo 는 1k/2k 분압 뒤)
constexpr uint8_t PIN_LED_R = 21, PIN_LED_G = 22, PIN_LED_B = 23;
constexpr uint8_t PIN_VBAT = 34;                             // 100k/33k 분압
constexpr uint8_t PIN_HALL = 36;                             // KY-024 A0 (도주차), 3.3V 로 켬
// PWM
constexpr uint8_t CH_MOTOR_L = 0, CH_MOTOR_R = 1, CH_LED_R = 2, CH_LED_G = 3, CH_LED_B = 4;
constexpr uint32_t MOTOR_PWM_HZ_DEFAULT = 1000;  // L298N 은 느려서 1kHz (점검 프로그램 f 로 바꿈)
constexpr uint32_t LED_PWM_HZ = 5000;
constexpr uint8_t PWM_BITS = 8;
// 주행
constexpr float V_TARGET = 5.5f;   // 모터에 주고 싶은 최대 전압
constexpr float V_DROP = 2.0f;     // L298N 전압 강하 (추정값)
constexpr int MIN_DUTY_DEFAULT = 90;
constexpr int RAMP_STEP = 13;      // 10ms 마다 최대 13 → 0→255 약 200ms
constexpr uint32_t CONTROL_PERIOD_MS = 10;
constexpr float SLOW_CM = 10.0f, STOP_CM = 3.0f, SLOW_FACTOR = 0.4f;  // 추격차 전진 제한
constexpr float STEER_GAIN = 0.6f;
constexpr bool PAD_INVERT_Y = false;  // 조이스틱 위가 후진이면 true
constexpr uint32_t BRAKE_HOLD_MS = 150;
// 폰 입력이 이 시간 동안 안 오면 정지 (설계서 6장 "0.5초 동안 새 명령 없음").
// 폰 웹 조이스틱 입력이 끊기면 차가 계속 달리지 않도록 이 시간 뒤 정지.
constexpr uint32_t PAD_RX_TIMEOUT_MS = 500;
// 배터리
constexpr float VBAT_K_DEFAULT = (100000.0f + 33000.0f) / 33000.0f;  // 약 4.03
// 초음파
constexpr uint32_t ECHO_TIMEOUT_US = 12000;  // 약 2m
constexpr uint32_t SONAR_PERIOD_MS = 60;
// 초음파 센서를 달면 true. false 면 초음파를 쏘지 않음 (앞 거리 = 미검출, 앞 거리 제한 없음)
constexpr bool SONAR_INSTALLED = false;
// 홀 센서
constexpr float HALL_THRESHOLD_MV_DEFAULT = 30.0f;
// LED
constexpr bool LED_COMMON_ANODE = false;
constexpr uint8_t LED_LEVEL = 80;
// 무선
constexpr uint8_t WIFI_CHANNEL = 1;
constexpr const char* AP_PASS_PREFIX = "rcartag";  // 비밀번호 = 이 글자 + 차 번호 (8글자)
constexpr int AP_MAX_CLIENTS = 1;
constexpr uint16_t HTTP_PORT = 80, WS_PORT = 81;
constexpr uint32_t PHONE_GRACE_MS = 3000;          // 폰이 이만큼 끊겨도 phoneOk 유지
constexpr uint32_t UI_PUSH_MS = 100;               // 상태를 폰에 보내는 주기
// 게임
constexpr uint32_t RUNNER_OFFLINE_CATCH_MS = 10000;  // 꺼진 참가 도주차는 이 시간 뒤 잡힌 것으로 (Game 설정에 넘김)
constexpr uint16_t GAME_SEC_DEFAULT = 180;
}  // namespace cfg

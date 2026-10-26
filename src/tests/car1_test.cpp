/*
 * =====================================================================
 *  RC카 하드웨어 점검 프로그램 (car1_test)          v2.0  2026-10-17
 * =====================================================================
 *  L298N판입니다. 배선 가이드(rc-car-l298n-wiring.html) 12단계 다음에 씁니다.
 *  USB 시리얼 모니터(115200)에서 "명령 글자 + Enter".
 *
 *  올리기 / 보기 (프로젝트 폴더에서)
 *    pio run -e car1_test -t upload
 *    pio device monitor -e car1_test
 *
 *  ※ 안전 수칙
 *    - USB 로 올릴 때는 "5V 레일 → ESP32 5V 핀" 선(P5)을 뽑으세요.
 *    - 모터 시험(m · n · d)은 바퀴를 띄운 채로, 정지 시험(s)은 바퀴를 손으로 잡고 하세요.
 *    - 모터가 도는 중 아무 글자 + Enter = 즉시 정지 (x 도 같음)
 *    - 최대 듀티는 배터리 전압으로 자동 계산합니다 (255 × 5.5 ÷ (배터리 − 2.0), 최대 255)
 *
 *  명령
 *    ?  도움말            p  배선 확인 (핀 하나씩 HIGH)     a  자동 점검
 *    l  LED              b  배터리 측정    c V 배터리 보정 (예: c 7.52)
 *    u  초음파 거리       m  모터 방향      n  최소 듀티      d  주행 데모
 *    s  정지 시험(전압 강하)                 x  모터 정지
 *    g  홀 센서 측정 (도주차)                k  잡힘 기준 저장 (자석 5cm)  예: k / k 35
 *    r  차 번호·역할      예: r 1 chaser / r 2 runner
 *    f  모터 PWM 주파수   예: f 1000
 *    i  설정 보기 / i reset 초기화
 * =====================================================================
 */
#include <Arduino.h>
#include <esp_system.h>
#include "analog_inputs.h"
#include "drive_math.h"
#include "motor_l298n.h"
#include "rcar_config.h"
#include "rgb_led.h"
#include "settings.h"
#include "sonar.h"

#define CAR1_TEST_VERSION "v2.0 (2026-10-17, L298N)"

constexpr int      DUTY_TEST = 130;        // 방향·동작 시험용 (약 51%)
constexpr float    TURN_INNER_RATIO = 0.4f;
constexpr uint32_t RAMP_MS = 200;
constexpr float    V_FULL = 8.4f, V_WARN = 6.8f, V_STOP = 6.4f;

CarSettings S;                                // NVS "rcar" 설정
L298NMotor motorL(cfg::PIN_ENA, cfg::PIN_IN1, cfg::PIN_IN2, cfg::CH_MOTOR_L);
L298NMotor motorR(cfg::PIN_ENB, cfg::PIN_IN3, cfg::PIN_IN4, cfg::CH_MOTOR_R);

struct MotorRef { L298NMotor* m; const char* name; bool left; };
MotorRef refL{&motorL, "왼쪽", true};
MotorRef refR{&motorR, "오른쪽", false};

// ============================ LED · 핀 준비 · 설정 ============================
void ledColor(uint8_t r, uint8_t g, uint8_t b) { ledWrite({r, g, b}); }

void setupPins() {
  motorL.begin(cfg::MOTOR_PWM_HZ_DEFAULT);   // 가장 먼저: 모터가 돌지 않게 (EN = 0)
  motorR.begin(cfg::MOTOR_PWM_HZ_DEFAULT);
  ledBegin();
  sonarBegin();
  analogInputsBegin();
}

void applySettings() {
  motorL.begin(S.pwmHz);
  motorR.begin(S.pwmHz);
  motorL.setInvert(S.invertL);
  motorR.setInvert(S.invertR);
}

const char* resetReasonName(esp_reset_reason_t r) {
  switch (r) {
    case ESP_RST_POWERON:   return "전원을 켬";
    case ESP_RST_SW:        return "소프트웨어 리셋";
    case ESP_RST_EXT:       return "리셋 핀";
    case ESP_RST_PANIC:     return "프로그램 오류(패닉)";
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT:       return "워치독";
    case ESP_RST_BROWNOUT:  return "전압 부족(브라운아웃) !!  전원선 · 커패시터 · 납땜을 확인하세요";
    case ESP_RST_DEEPSLEEP: return "딥슬립 복귀";
    default:                return "알 수 없음";
  }
}

// ============================ 입력 도우미 ============================
// 남아 있는 입력을 모두 버립니다. (Enter 의 CR/LF 찌꺼기가 "정지 신호"로 오해되지 않게)
void flushInput() {
  delay(10);
  while (Serial.available()) { Serial.read(); delay(2); }
}

// 입력이 들어왔는지 확인하고 (있으면 전부 비움) 알려 줍니다. 동작 중 "멈춰!" 신호로 씁니다.
bool abortRequested() {
  if (!Serial.available()) return false;
  flushInput();
  return true;
}

// ms 동안 기다리되, 입력이 들어오면 true (= 중단)
bool waitOrAbort(uint32_t ms) {
  uint32_t t0 = millis();
  while (millis() - t0 < ms) {
    if (abortRequested()) return true;
    delay(10);
  }
  return false;
}

// Enter 까지 한 줄 읽기 (빈 줄은 무시). 시리얼 모니터는 친 글자를 화면에 보여 주지 않으므로
// 여기서 직접 되돌려 보여 줍니다(에코). idleMs 동안 아무 글자도 안 오면 지금까지 쓴 글자(보통 "") 반환.
String readLine(uint32_t idleMs) {
  String s;
  uint32_t t0 = millis();
  while (millis() - t0 < idleMs) {
    while (Serial.available()) {
      char c = (char)Serial.read();
      t0 = millis();                              // 글자가 올 때마다 기다리는 시간을 다시 잼
      if (c == '\n' || c == '\r') {
        if (s.length() > 0) {
          Serial.println();
          flushInput();                           // CR+LF 의 나머지 글자 버리기
          s.trim();
          return s;
        }
      } else if (c == 8 || c == 127) {            // 백스페이스
        if (s.length() > 0) { s.remove(s.length() - 1); Serial.print("\b \b"); }
      } else {
        s += c;
        Serial.write((uint8_t)c);
      }
    }
    delay(5);
  }
  s.trim();
  return s;
}

bool askYesNo(const char* question) {
  Serial.printf("%s (y/n): ", question);
  String a = readLine(60000);
  if (a.length() == 0) { Serial.println("\n  입력이 없어 취소합니다."); return false; }
  return a[0] == 'y' || a[0] == 'Y';
}

int askChoice(const char* question, int count) {
  Serial.printf("%s  [1~%d]: ", question, count);
  String a = readLine(60000);
  if (a.length() == 0) Serial.println();
  int v = a.toInt();
  return (v >= 1 && v <= count) ? v : 0;
}

// Enter 만 치면 true (계속), q + Enter 이거나 시간 초과면 false (그만)
bool waitEnterOrQuit(uint32_t timeoutMs) {
  bool quit = false;
  uint32_t t0 = millis();
  while (millis() - t0 < timeoutMs) {
    while (Serial.available()) {
      char c = (char)Serial.read();
      if (c == 'q' || c == 'Q') quit = true;
      if (c == '\n' || c == '\r') { flushInput(); return !quit; }   // CR+LF 의 나머지 글자도 버림
    }
    delay(5);
  }
  return false;
}

// ============================ 모터 ============================
float readVbat();

int dutyMax() { return rcar::maxDutyForBattery(readVbat(), cfg::V_TARGET, cfg::V_DROP); }

void allStop() {
  motorL.coast();
  motorR.coast();
}

// 왼쪽 · 오른쪽 목표 듀티로 rampMs 동안 서서히 올리고 holdMs 유지한 뒤 정지. 입력이 오면 즉시 정지 + false
bool driveFor(int left, int right, uint32_t holdMs, uint32_t rampMs = RAMP_MS) {
  flushInput();
  int lim = dutyMax();
  left = constrain(left, -lim, lim);
  right = constrain(right, -lim, lim);
  uint32_t t0 = millis();
  while (true) {
    uint32_t t = millis() - t0;
    if (t >= rampMs + holdMs) break;
    float f = (t < rampMs) ? (float)t / (float)rampMs : 1.0f;
    motorL.set((int)(left * f));
    motorR.set((int)(right * f));
    if (abortRequested()) {
      allStop();
      Serial.println("  [정지] 입력이 들어와 즉시 멈췄습니다.");
      return false;
    }
    delay(10);
  }
  allStop();
  return true;
}

// ============================ 배터리 ============================
float readVpin() { return batteryPinVolts(); }
float readVbat() { return readVpin() * S.vbatK; }

const char* batteryStatus(float v) {
  if (v < 3.0f)         return "배터리 측정 안 함 (34번 핀이 GND 에 연결된 최소 구성이면 정상) 또는 배터리 꺼짐";
  if (v < V_STOP)       return "사용 중지 수준 (6.4V 미만) — 충전하세요";
  if (v < V_WARN)       return "경고 (6.8V 미만) — 곧 충전";
  if (v <= V_FULL+0.2f) return "정상";
  return "너무 높게 읽힘 — 보정(c)이나 분압 회로 확인";
}

// 모터 시험을 해도 되는 상태인지 확인
bool batteryOkForMotors() {
  float v = readVbat();
  if (v < 3.0f) {
    // "[경고] 배터리가 %.2f V 로 읽힙니다 (0V 근처)."
    // "  → 이번 구성은 배터리를 재지 않습니다 (GPIO34 를 GND 에 연결). 그래서 0V 가 정상이니 아래 질문에 y 를 치세요."
    // "  → 모터가 안 돌면 배터리 2개가 홀더에 다 들어 있는지 확인하세요."
    Serial.printf("[\xEA\xB2\xBD\xEA\xB3\xA0] \xEB\xB0\xB0\xED\x84\xB0\xEB\xA6\xAC\xEA\xB0\x80 %.2f V \xEB\xA1\x9C \xEC\x9D\xBD\xED\x9E\x99\xEB\x8B\x88\xEB\x8B\xA4 (0V \xEA\xB7\xBC\xEC\xB2\x98).\n"
                  "  \xE2\x86\x92 \xEC\x9D\xB4\xEB\xB2\x88 \xEA\xB5\xAC\xEC\x84\xB1\xEC\x9D\x80 \xEB\xB0\xB0\xED\x84\xB0\xEB\xA6\xAC\xEB\xA5\xBC \xEC\x9E\xAC\xEC\xA7\x80 \xEC\x95\x8A\xEC\x8A\xB5\xEB\x8B\x88\xEB\x8B\xA4 (GPIO34 \xEB\xA5\xBC GND \xEC\x97\x90 \xEC\x97\xB0\xEA\xB2\xB0). \xEA\xB7\xB8\xEB\x9E\x98\xEC\x84\x9C 0V \xEA\xB0\x80 \xEC\xA0\x95\xEC\x83\x81\xEC\x9D\xB4\xEB\x8B\x88 \xEC\x95\x84\xEB\x9E\x98 \xEC\xA7\x88\xEB\xAC\xB8\xEC\x97\x90 y \xEB\xA5\xBC \xEC\xB9\x98\xEC\x84\xB8\xEC\x9A\x94.\n"
                  "  \xE2\x86\x92 \xEB\xAA\xA8\xED\x84\xB0\xEA\xB0\x80 \xEC\x95\x88 \xEB\x8F\x8C\xEB\xA9\xB4 \xEB\xB0\xB0\xED\x84\xB0\xEB\xA6\xAC 2\xEA\xB0\x9C\xEA\xB0\x80 \xED\x99\x80\xEB\x8D\x94\xEC\x97\x90 \xEB\x8B\xA4 \xEB\x93\xA4\xEC\x96\xB4 \xEC\x9E\x88\xEB\x8A\x94\xEC\xA7\x80 \xED\x99\x95\xEC\x9D\xB8\xED\x95\x98\xEC\x84\xB8\xEC\x9A\x94.\n", v);
    // "그래도 모터를 시험할까요? (이번 구성은 y 가 정상)"
    return askYesNo("\xEA\xB7\xB8\xEB\x9E\x98\xEB\x8F\x84 \xEB\xAA\xA8\xED\x84\xB0\xEB\xA5\xBC \xEC\x8B\x9C\xED\x97\x98\xED\x95\xA0\xEA\xB9\x8C\xEC\x9A\x94? (\xEC\x9D\xB4\xEB\xB2\x88 \xEA\xB5\xAC\xEC\x84\xB1\xEC\x9D\x80 y \xEA\xB0\x80 \xEC\xA0\x95\xEC\x83\x81)");
  }
  if (v < V_STOP) {
    Serial.printf("[중지] 배터리 %.2f V — %.1f V 미만이라 모터 시험을 하지 않습니다. 충전하세요.\n", v, V_STOP);
    return false;
  }
  if (v > V_FULL + 0.3f) {
    Serial.printf("[경고] 배터리 %.2f V 는 만충(8.4V)보다 높게 읽힙니다. 측정 회로(100kΩ/33kΩ 분압)를 만든 경우 보정(c)과 분압 저항을 확인하세요.\n", v);
    return askYesNo("그래도 진행할까요?");
  }
  return true;
}

// ============================ 초음파 ============================
// 거리(cm). 에코가 안 돌아오면 -1 (앞이 트였거나 약 2m 밖)
float readDistanceCm() { return sonarReadCm(); }

// ============================ 명령들 ============================
void printMenu() {
  Serial.println();
  Serial.println("================ 점검 메뉴 (명령 글자 + Enter) ================");
  Serial.println(" ?  도움말               p  배선 확인 (핀 하나씩 HIGH)");
  Serial.println(" a  자동 점검            l  LED 시험");
  Serial.println(" b  배터리 측정          c V 배터리 보정 (예: c 7.52)");
  Serial.println(" u  초음파 거리          m  모터 방향 (바퀴 띄움)");
  Serial.println(" n  최소 듀티 (바퀴 띄움) d  주행 데모 (바퀴 띄움)");
  Serial.println(" s  정지 시험 (바퀴 잡음) x  모터 정지");
  Serial.println(" g  홀 센서 측정 (도주차) k  잡힘 기준 저장 (자석 5cm)  예: k / k 35");
  Serial.println(" r  차 번호·역할  예: r 1 chaser / r 2 runner");
  Serial.println(" f  모터 PWM 주파수  예: f 1000      i  설정 보기 / i reset");
  Serial.println("================================================================");
}

void cmdLed() {
  Serial.println("\n[LED 시험] 빨강 → 초록 → 파랑 → 흰색 → 꺼짐 (각 0.7초)");
  struct ColorStep { const char* name; uint8_t r, g, b; };
  const ColorStep steps[] = {
    {"빨강", cfg::LED_LEVEL, 0, 0}, {"초록", 0, cfg::LED_LEVEL, 0}, {"파랑", 0, 0, cfg::LED_LEVEL},
    {"흰색", cfg::LED_LEVEL, cfg::LED_LEVEL, cfg::LED_LEVEL},
  };
  for (const ColorStep& s : steps) {
    Serial.printf("  %s\n", s.name);
    ledColor(s.r, s.g, s.b);
    if (waitOrAbort(700)) break;
  }
  ledColor(0, 0, 0);
  Serial.println("  → 색이 하나라도 안 켜지면 그 색의 핀 이름(R · G · B)과 점퍼선 연결을 확인하세요. (초록 · 파랑이 빨강보다 어두운 것은 3.3V 구동이라 정상)");
  Serial.println("  → 아무 색도 안 켜지면 LED 모듈이 공통 애노드형(공통 핀이 +)일 수 있어요: 그 핀을 GND 대신 3V3 에 잇고, lib/rcar_hw/src/rcar_config.h 의 LED_COMMON_ANODE 를 true 로 바꿔 다시 올리세요.");
}

void cmdBattery() {
  Serial.println("\n[배터리 측정] 2초 동안 10번 읽습니다.");
  float sum = 0, vmin = 99.0f, vmax = 0.0f;
  int n = 0;
  for (int i = 0; i < 10; i++) {
    float vp = readVpin();
    float vb = vp * S.vbatK;
    sum += vb;  n++;
    vmin = min(vmin, vb);  vmax = max(vmax, vb);
    Serial.printf("  34번 핀 %.3f V  →  배터리 %.2f V\n", vp, vb);
    if (waitOrAbort(200)) break;
  }
  if (n == 0) return;
  float avg = sum / n;
  Serial.printf("평균 %.2f V  (최소 %.2f ~ 최대 %.2f)  →  %s\n", avg, vmin, vmax, batteryStatus(avg));
  Serial.printf("보정계수 %.3f  (기본 %.3f)\n", S.vbatK, cfg::VBAT_K_DEFAULT);
  Serial.printf("참고: 이 전압에서 최대 듀티 = %d / 255 (255 × 5.5 ÷ (배터리 − 2.0))\n",
                rcar::maxDutyForBattery(avg, cfg::V_TARGET, cfg::V_DROP));
  Serial.println("→ 멀티미터로 배터리 전압을 재서 ±0.2V 이내면 합격. 다르면 \"c 멀티미터값\" 으로 보정하세요.");
}

void cmdCalibrate(const String& arg) {
  if (arg.equalsIgnoreCase("reset")) {
    S.vbatK = cfg::VBAT_K_DEFAULT;
    settingsSave(S);
    Serial.printf("보정계수를 기본값 %.3f 로 되돌렸습니다.\n", S.vbatK);
    return;
  }
  float measured = arg.toFloat();
  if (measured <= 0.0f) {
    Serial.println("사용법: c 7.52   (멀티미터로 잰 배터리 전압을 숫자로)   /   c reset : 기본값으로");
    return;
  }
  if (measured < 3.0f || measured > 9.0f) {
    Serial.println("입력한 값이 이상해요. 2개 직렬 배터리는 보통 6.0 ~ 8.4 V 입니다.");
    return;
  }
  float vp = readVpin();
  if (vp < 0.2f) {
    Serial.println("34번 핀 전압이 너무 낮아요 (배터리 측정 회로가 없는 최소 구성이면 보정은 필요 없습니다 / 측정 회로가 있다면 분압 선 확인). 보정하지 않았습니다.");
    return;
  }
  float k = measured / vp;
  if (k < 3.3f || k > 4.8f) {
    Serial.printf("계산된 보정계수 %.3f 가 기본값(%.3f)과 너무 달라요. (측정 회로를 만든 경우) 100kΩ/33kΩ 저항 값과 배선을 먼저 확인하세요.\n", k, cfg::VBAT_K_DEFAULT);
    return;
  }
  S.vbatK = k;
  settingsSave(S);
  Serial.printf("보정 완료: 보정계수 %.3f (저장됨). 이제 배터리 = %.2f V 로 읽힙니다. (b 로 다시 확인)\n", S.vbatK, readVbat());
}

void cmdUltra() {
  Serial.println("\n[초음파 거리] 0.1초마다 측정합니다. Enter 를 치면 끝납니다.");
  Serial.println("  손바닥을 10cm, 30cm 앞에 대 보세요. 앞이 트여 있으면 '미검출'이 나오는 것이 정상입니다.");
  int total = 0, valid = 0;
  float dmin = 1e9f, dmax = 0.0f;
  flushInput();
  while (!abortRequested()) {
    float d = readDistanceCm();
    total++;
    if (d < 0) {
      Serial.println("  미검출 (에코 없음)");
    } else {
      valid++;
      dmin = min(dmin, d);  dmax = max(dmax, d);
      Serial.printf("  %.1f cm\n", d);
    }
    delay(100);                                   // 측정 주기 60ms 이상 (데이터시트)
  }
  Serial.printf("끝: %d번 중 %d번 검출", total, valid);
  if (valid > 0) Serial.printf(", 최소 %.1f ~ 최대 %.1f cm", dmin, dmax);
  Serial.println();
  if (total >= 5 && valid == 0) {
    Serial.println("  → 현재 구성에는 초음파 센서를 달지 않았으므로 \"미검출\"이 정상입니다. 나중에 센서를 달면 Trig=GPIO18 / Echo=GPIO19 (Echo 에 1kΩ/2kΩ 분압 필수), VCC 5V · GND 를 확인하세요.");
  }
}

void cmdDirection() {
  Serial.println("\n[모터 방향 시험]  ※ 바퀴를 바닥에서 띄우세요 (차를 들거나 받침 위에)");
  Serial.println("  (왼쪽 · 오른쪽 = 차를 뒤에서 앞쪽으로 바라봤을 때의 왼쪽 · 오른쪽)");
  if (!batteryOkForMotors()) return;
  if (!askYesNo("바퀴가 떠 있고 배터리 2개가 들어 있나요?")) return;
  bool changed = false;
  MotorRef* motors[] = {&refL, &refR};
  for (MotorRef* m : motors) {
    bool isLeft = m->left;
    Serial.printf("\n--- %s 모터: 전진 1.5초 (이 바퀴만) ---\n", m->name);
    Serial.println("  차의 '앞쪽'으로 굴러가는 방향이면 정상입니다. (중간에 아무 글자 + Enter = 즉시 정지)");
    if (!(isLeft ? driveFor(DUTY_TEST, 0, 1500) : driveFor(0, DUTY_TEST, 1500))) return;
    delay(300);
    int c = askChoice("결과는?  1=앞으로 돌았다  2=뒤로(반대) 돌았다  3=안 돌았다", 3);
    if (c == 2) {
      bool inv = !m->m->inverted();
      m->m->setInvert(inv);
      if (isLeft) S.invertL = inv; else S.invertR = inv;
      settingsSave(S);
      changed = true;
      Serial.printf("  → %s 모터 방향 반전을 %s 로 저장했습니다. (OUT 단자 두 선을 바꿔 물려도 됩니다)\n", m->name, inv ? "켬" : "끔");
    } else if (c == 3) {
      Serial.println("  → 안 돌면 확인: ① ENA/ENB 선이 5V 핀이 아닌 입력 핀에 있나 (배선 가이드 8단계) ② ENA/ENB 캡을 뺐나");
      Serial.println("                  ③ L298N +12V 에 배터리 전압 ④ OUT 단자 나사 · 모터 납땜 ⑤ 듀티가 너무 낮은지 (n)");
      continue;
    } else if (c == 0) {
      Serial.println("  입력이 없어 이 모터는 건너뜁니다.");
      continue;
    }
    // 후진도 확인 (IN 두 선이 모두 맞게 연결됐는지)
    Serial.printf("--- %s 모터: 후진 1.5초 ---\n", m->name);
    if (!(isLeft ? driveFor(-DUTY_TEST, 0, 1500) : driveFor(0, -DUTY_TEST, 1500))) return;
    delay(300);
    if (!askYesNo("  전진과 반대 방향으로 돌았나요?")) {
      Serial.println("  → 후진이 안 되면 그 모터의 IN2/IN4 또는 IN1/IN3 선 연결을 확인하세요.");
    }
  }
  allStop();
  Serial.println(changed ? "\n방향 설정이 바뀌었습니다. m 으로 다시 한 번 확인하세요." : "\n방향 시험 끝.");
}

void cmdMinDuty() {
  Serial.println("\n[최소 듀티 찾기]  ※ 바퀴를 띄우세요.");
  Serial.println("  듀티를 30부터 5씩 올립니다. 바퀴가 '돌기 시작하는 순간' Enter 를 치세요. (왼쪽 → 오른쪽 순서)");
  if (!batteryOkForMotors()) return;
  if (!askYesNo("바퀴가 떠 있고 배터리 2개가 들어 있나요?")) return;
  MotorRef* motors[] = {&refL, &refR};
  for (MotorRef* m : motors) {
    bool isLeft = m->left;
    Serial.printf("\n--- %s 모터 --- (돌기 시작하면 Enter)\n", m->name);
    int found = -1;
    for (int duty = 30; duty <= 150; duty += 5) {
      m->m->set(duty);
      Serial.printf("  듀티 %d (%.0f%%)\n", duty, duty * 100.0f / 255.0f);
      if (waitOrAbort(600)) { found = duty; break; }
    }
    allStop();
    if (found < 0) {
      Serial.println("  150 까지 올려도 안 돌았다면 배선 · 전원을 먼저 확인하세요 (m 명령).");
      continue;
    }
    Serial.printf("  → %s 모터가 %d 부터 돌기 시작. 안전하게 쓰려면 최소 듀티를 약 %d 로 잡으세요 (여유 +10).\n", m->name, found, found + 10);
    if (isLeft) S.minDutyL = found + 10; else S.minDutyR = found + 10;
    settingsSave(S);
    delay(400);
  }
  Serial.println("\n(본 프로그램이 이 최소 듀티를 씁니다. i 로 볼 수 있어요.)");
}

void cmdDrive() {
  Serial.println("\n[주행 동작 데모]  ※ 바퀴를 띄우세요. 각 동작 후 정지합니다. (아무 글자 + Enter = 중단)");
  if (!batteryOkForMotors()) return;
  if (!askYesNo("바퀴가 떠 있고 배터리 2개가 들어 있나요?")) return;
  struct Move { const char* name; int l, r; uint32_t ms; const char* expect; };
  const int T = DUTY_TEST;
  const int top = dutyMax();
  const int inner = (int)(top * TURN_INNER_RATIO);   // 안쪽 바퀴 = 바깥(최대)의 40%
  const Move moves[] = {
    {"전진",          T,  T,  2000, "두 바퀴 모두 앞으로"},
    {"후진",         -T, -T,  2000, "두 바퀴 모두 뒤로"},
    {"좌회전",    inner, top, 2000, "왼쪽 바퀴가 오른쪽보다 느리게 (최소 듀티보다 낮으면 멈춰 있을 수 있음)"},
    {"우회전",      top, inner, 2000, "오른쪽 바퀴가 왼쪽보다 느리게 (최소 듀티보다 낮으면 멈춰 있을 수 있음)"},
    {"제자리 좌회전", -T, T,   1500, "왼쪽은 뒤로, 오른쪽은 앞으로"},
    {"제자리 우회전",  T, -T,  1500, "왼쪽은 앞으로, 오른쪽은 뒤로"},
  };
  for (const Move& mv : moves) {
    Serial.printf("\n▶ %s (왼쪽 %d, 오른쪽 %d) — %s\n", mv.name, mv.l, mv.r, mv.expect);
    if (!driveFor(mv.l, mv.r, mv.ms)) return;
    if (waitOrAbort(600)) return;
  }
  Serial.println("\n끝. 동작이 기대와 다르면 m(방향)과 p(배선 확인)부터 다시 보세요.");
}

// 정지(스톨) 시험 — 전압 강하
void cmdStall() {
  Serial.println("\n[정지 시험: 전압 강하]  ※ 두 바퀴를 손으로 꽉 잡아 못 돌게 하세요. 모터는 딱 1초만 돕니다.");
  Serial.println("  1) 멀티미터(DC V)를 '배터리 홀더의 + 단자' 와 'L298N +12V 단자' 사이에 댑니다.");
  Serial.println("  2) 아래 시작 신호 후 1초 동안 멀티미터에 보이는 값 = 가는 길에서 사라진 전압.  합격: 1.0V 이하");
  Serial.println("  3) 시험 중 ESP32 가 리셋되거나 전원이 끊기면 그 자체가 불합격 신호입니다 (납땜 · 접촉 · 전선 굵기 확인).");
  if (!batteryOkForMotors()) return;
  if (!askYesNo("바퀴를 잡았고 멀티미터를 연결했나요?")) return;
  Serial.println("3초 뒤 시작합니다. (취소: 아무 글자 + Enter)");
  for (int i = 3; i > 0; i--) {
    Serial.printf("  %d...\n", i);
    if (waitOrAbort(1000)) { Serial.println("  취소했습니다."); return; }
  }
  float before = readVbat();
  float vmin = before;
  int top = dutyMax();
  uint32_t t0 = millis();
  Serial.println("  >>> 구동 중 (1초)");
  while (millis() - t0 < 1000) {
    float f = min(1.0f, (millis() - t0) / 100.0f);          // 0.1초 소프트 스타트 후 최대 듀티 유지
    motorL.set((int)(top * f));
    motorR.set((int)(top * f));
    float v = readVbat();
    if (v < vmin) vmin = v;
    delay(15);
  }
  allStop();
  delay(400);
  float after = readVbat();
  Serial.println("  <<< 정지");
  Serial.printf("배터리(34번 핀 기준): 시험 전 %.2f V → 구동 중 최저 %.2f V (처짐 %.2f V) → 정지 후 %.2f V\n", before, vmin, before - vmin, after);
  Serial.println("  ※ 이 값은 측정선에서 잰 배터리 쪽 처짐입니다. 합격 판정은 멀티미터로 본 '배터리 + ↔ L298N +12V' 값(1.0V 이하)으로 하세요.");
  Serial.println("  → 1.0V 를 넘으면: 납땜 불량 · 접촉 불량 · 가는 전선 · 배터리 홀더 선을 의심하세요.");
  Serial.println("  → 시험 직후 L298N 방열판 · 배터리 홀더 선이 뜨겁지 않은지 손으로 확인하세요.");
}

// 배선 확인 — 핀을 하나씩 HIGH 로 만들고 멀티미터로 해당 핀이 3.3V 인지 확인
void cmdPins() {
  struct PinStep { const char* gpio; const char* target; uint8_t pin; int8_t ch; };
  const PinStep list[] = {
    {"GPIO25", "L298N ENA (왼쪽 속도)", cfg::PIN_ENA, (int8_t)cfg::CH_MOTOR_L},
    {"GPIO26", "L298N IN1 (왼쪽 방향)", cfg::PIN_IN1, -1},
    {"GPIO27", "L298N IN2 (왼쪽 방향)", cfg::PIN_IN2, -1},
    {"GPIO13", "L298N ENB (오른쪽 속도)", cfg::PIN_ENB, (int8_t)cfg::CH_MOTOR_R},
    {"GPIO32", "L298N IN3 (오른쪽 방향)", cfg::PIN_IN3, -1},
    {"GPIO33", "L298N IN4 (오른쪽 방향)", cfg::PIN_IN4, -1},
    // "초음파 Trig (센서를 안 단 구성이면 재지 말고 Enter)"
    {"GPIO18", "\xEC\xB4\x88\xEC\x9D\x8C\xED\x8C\x8C Trig (\xEC\x84\xBC\xEC\x84\x9C\xEB\xA5\xBC \xEC\x95\x88 \xEB\x8B\xA8 \xEA\xB5\xAC\xEC\x84\xB1\xEC\x9D\xB4\xEB\xA9\xB4 \xEC\x9E\xAC\xEC\xA7\x80 \xEB\xA7\x90\xEA\xB3\xA0 Enter)", cfg::PIN_TRIG, -1},
  };
  Serial.println("\n[배선 확인] 핀을 하나씩 3.3V(HIGH)로 만듭니다. 한 번에 한 핀만 켜므로 모터는 돌지 않습니다.");
  Serial.println("  멀티미터(DC V)의 검정 프로브는 GND, 빨강 프로브는 아래 안내한 모듈 핀에 대세요. 약 3.3V 면 정상, 0V 면 선이 빠졌거나 다른 핀에 꽂힌 것입니다.");
  Serial.println("  Enter = 다음 핀, q + Enter = 끝내기");
  allStop();
  for (const PinStep& p : list) {
    Serial.printf("\n> %s = HIGH  →  %s 핀을 재 보세요 (약 3.3V)\n", p.gpio, p.target);
    if (p.ch >= 0) ledcWrite(p.ch, 255); else digitalWrite(p.pin, HIGH);
    bool next = waitEnterOrQuit(180000);
    if (p.ch >= 0) ledcWrite(p.ch, 0); else digitalWrite(p.pin, LOW);
    if (!next) break;
  }
  allStop();
  Serial.println("\n끝. 이 프로그램이 못 보는 전원은 멀티미터로 직접 확인하세요:");
  Serial.println("  · L298N +12V = 배터리 전압(배터리 넣은 상태)   · L298N +5V · 브레드보드 5V 레일 = 4.8~5.2V   · (도주차) KY-024 + = 3.3V");
}

// ============================ 홀 센서 · 역할 · PWM ============================
float hallBaseline = 0.0f;   // g 를 처음 실행할 때 잰 기준값 (mV)

float measureHallAverage(uint32_t ms) {
  float sum = 0; int n = 0;
  uint32_t t0 = millis();
  while (millis() - t0 < ms) { sum += hallReadMv(); n++; delay(10); }
  return n ? sum / n : 0.0f;
}

void cmdHall() {
  Serial.println("\n[홀 센서 측정] 자석을 센서에서 멀리(30cm 이상) 두세요. 1초 동안 기준값을 잽니다.");
  hallBaseline = measureHallAverage(1000);
  Serial.printf("  기준값 %.1f mV (정상 범위 약 1500~1800)\n", hallBaseline);
  if (hallBaseline < 300.0f || hallBaseline > 3000.0f) {
    Serial.println("  → 범위를 벗어났어요: KY-024 + = 3V3 · G = GND · A0 = VP(GPIO36) 인지, D0 와 바꿔 꽂지 않았는지 확인하세요.");
  }
  Serial.println("  이제 자석을 1, 2, 3 ... 8cm 거리에 차례로 두고 '변화량'을 기록하세요. Enter 로 끝.");
  Serial.printf("  (지금 잡힘 기준: %.1f mV)\n", S.hallThresholdMv);
  float maxDelta = 0;
  flushInput();
  while (!abortRequested()) {
    float mv = hallReadMv();
    float d = fabsf(mv - hallBaseline);
    if (d > maxDelta) maxDelta = d;
    Serial.printf("  %.1f mV   변화량 %.1f mV %s\n", mv, d, d >= S.hallThresholdMv ? "← 잡힘 범위" : "");
    delay(200);
  }
  Serial.printf("끝. 가장 큰 변화량 %.1f mV. 자석을 5cm 에 두고 k 를 입력하면 그 값을 잡힘 기준으로 저장합니다.\n", maxDelta);
}

void cmdThreshold(const String& arg) {
  float v = arg.toFloat();
  if (v <= 0.0f) {
    if (hallBaseline <= 0.0f) { Serial.println("먼저 g 로 기준값을 재세요 (자석 멀리)."); return; }
    Serial.println("[잡힘 기준] 자석을 센서에서 정확히 5cm, 같은 높이에 두세요. 1초 동안 잽니다.");
    if (waitOrAbort(1500)) return;
    v = fabsf(measureHallAverage(1000) - hallBaseline);
    if (v < 10.0f) {
      Serial.printf("  변화량이 %.1f mV 로 너무 작아요 (10 미만). 자석이 더 크거나 가까워야 합니다. 저장하지 않았습니다.\n", v);
      return;
    }
  }
  S.hallThresholdMv = v;
  settingsSave(S);
  Serial.printf("잡힘 기준을 %.1f mV 로 저장했습니다. (6cm 에서 g 의 변화량이 이 값보다 작아야 합니다)\n", v);
}

void cmdRole(const String& arg) {
  int id = arg.toInt();
  String lower = arg; lower.toLowerCase();
  bool chaser = lower.indexOf("chaser") >= 0;
  bool runner = lower.indexOf("runner") >= 0;
  if (id < 1 || id > 4 || chaser == runner) {
    Serial.println("사용법: r 1 chaser   또는   r 2 runner   (차 번호 1~4, 추격 chaser / 도주 runner)");
    return;
  }
  S.carId = (uint8_t)id;
  S.role = chaser ? rcar::Role::Chaser : rcar::Role::Runner;
  settingsSave(S);
  // "저장: N번 차, 추격차. 폰 Wi-Fi 이름은 RCAR-N, 비밀번호는 rcartagN 가 됩니다."
  Serial.printf("\xEC\xA0\x80\xEC\x9E\xA5: %d\xEB\xB2\x88 \xEC\xB0\xA8, %s. \xED\x8F\xB0 Wi-Fi \xEC\x9D\xB4\xEB\xA6\x84\xEC\x9D\x80 RCAR-%d, \xEB\xB9\x84\xEB\xB0\x80\xEB\xB2\x88\xED\x98\xB8\xEB\x8A\x94 %s%d \xEA\xB0\x80 \xEB\x90\xA9\xEB\x8B\x88\xEB\x8B\xA4.\n", id,
                chaser ? "\xEC\xB6\x94\xEA\xB2\xA9\xEC\xB0\xA8" : "\xEB\x8F\x84\xEC\xA3\xBC\xEC\xB0\xA8", id, cfg::AP_PASS_PREFIX, id);
}

void cmdPwm(const String& arg) {
  long hz = arg.toInt();
  if (hz < 100 || hz > 20000) { Serial.println("사용법: f 1000   (100 ~ 20000 Hz)"); return; }
  S.pwmHz = (uint32_t)hz;
  settingsSave(S);
  allStop();
  applySettings();
  Serial.printf("모터 PWM 을 %ld Hz 로 저장했습니다. n(최소 듀티)을 다시 재 보세요.\n", hz);
}

void cmdSelfCheck() {
  Serial.println("\n=== 자동 점검 (모터는 따로: m → s) ===");
  // 1) 배터리
  float v = readVbat();
  Serial.printf("[%s] 배터리 %.2f V  (%s)\n", (v >= 7.0f && v <= V_FULL + 0.2f) ? "OK  " : "확인", v, batteryStatus(v));
  // 2) 초음파
  int valid = 0;
  float last = -1.0f;
  for (int i = 0; i < 6; i++) {
    float d = readDistanceCm();
    if (d > 0) { valid++; last = d; }
    delay(100);
  }
  if (valid > 0) Serial.printf("[OK  ] 초음파 6번 중 %d번 검출 (마지막 %.1f cm)\n", valid, last);
  else           Serial.println("[확인] 초음파 6번 모두 미검출 — 앞이 트였거나 배선 문제. 손을 10cm 앞에 대고 u 로 다시 보세요.");
  // 3) LED
  Serial.println("[눈으로] LED 가 빨강 → 초록 → 파랑 → 흰색으로 켜지는지 보세요.");
  cmdLed();
  Serial.println("\n다음: p(배선 확인) → m(모터 방향) → s(전압 강하) 순서로 진행하세요.");
}

void cmdInfo(const String& arg) {
  if (arg.equalsIgnoreCase("reset")) {
    settingsClear();
    S = settingsDefaults();
    applySettings();
    Serial.println("저장된 설정을 모두 지웠습니다 (차 번호 · 역할 포함).");
    return;
  }
  Serial.println("\n[설정]  (i reset = 초기화)");
  Serial.printf("  프로그램 버전      : %s\n", CAR1_TEST_VERSION);
  Serial.printf("  차 번호 · 역할     : %d번 · %s  (0 = 아직 안 정함, r 로 정하기)\n", S.carId, S.role == rcar::Role::Chaser ? "추격" : "도주");
  Serial.printf("  배터리 보정계수    : %.3f  (기본 %.3f)\n", S.vbatK, cfg::VBAT_K_DEFAULT);
  Serial.printf("  방향 반전 (왼/오)  : %s / %s\n", S.invertL ? "켬" : "끔", S.invertR ? "켬" : "끔");
  Serial.printf("  최소 듀티 (왼/오)  : %d / %d\n", S.minDutyL, S.minDutyR);
  Serial.printf("  모터 PWM           : %lu Hz,  지금 최대 듀티 %d / 255\n", (unsigned long)S.pwmHz, dutyMax());
  Serial.printf("  잡힘 기준          : %.1f mV\n", S.hallThresholdMv);
}

// ============================ Arduino ============================
void setup() {
  setupPins();                  // 가장 먼저: 모터가 돌지 않게
  Serial.begin(115200);
  delay(300);
  S = settingsLoad();
  applySettings();
  Serial.println();
  Serial.println("##################################################");
  Serial.printf ("#  RC카 점검 프로그램  %s\n", CAR1_TEST_VERSION);
  Serial.println("##################################################");
  Serial.printf ("직전 리셋 원인: %s\n", resetReasonName(esp_reset_reason()));
  float v = readVbat();
  Serial.printf ("배터리: %.2f V  (%s)\n", v, batteryStatus(v));
  Serial.printf ("차: %d번 %s  (r 로 바꾸기)\n", S.carId, S.role == rcar::Role::Chaser ? "추격" : "도주");
  Serial.println("※ 모터 시험은 바퀴를 띄우고, USB 를 꽂았다면 P5(5V 선)를 뽑은 상태에서 하세요.");
  printMenu();
}

void loop() {
  if (!Serial.available()) { delay(10); return; }
  String line = readLine(10000);                  // 천천히 쳐도 되도록 글자 사이 10초까지 기다림
  if (line.length() == 0) return;
  char cmd = (char)tolower(line.charAt(0));
  String arg = line.substring(1);
  arg.trim();
  switch (cmd) {
    case '?': case 'h': printMenu();       break;
    case 'a': cmdSelfCheck();              break;
    case 'p': cmdPins();                   break;
    case 'l': cmdLed();                    break;
    case 'b': cmdBattery();                break;
    case 'c': cmdCalibrate(arg);           break;
    case 'u': cmdUltra();                  break;
    case 'm': cmdDirection();              break;
    case 'n': cmdMinDuty();                break;
    case 'd': cmdDrive();                  break;
    case 's': cmdStall();                  break;
    case 'g': cmdHall();                   break;
    case 'k': cmdThreshold(arg);           break;
    case 'r': cmdRole(arg);                break;
    case 'f': cmdPwm(arg);                 break;
    case 'i': cmdInfo(arg);                break;
    case 'x': allStop(); ledColor(0, 0, 0); Serial.println("모터 정지."); break;
    default:  Serial.printf("모르는 명령 '%s' — ? 를 입력하면 메뉴가 나옵니다.\n", line.c_str()); break;
  }
  Serial.println("\n(명령을 입력하세요. ? = 메뉴)");
}

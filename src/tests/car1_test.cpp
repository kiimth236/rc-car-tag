/*
 * =====================================================================
 *  1호차 하드웨어 점검 프로그램 (car1_test)               v1.1  2026-10-14
 * =====================================================================
 *  무엇을 하나요?
 *    1호차(골든 유닛)를 조립하면서 배선과 부품이 맞는지 하나씩 확인합니다.
 *    설계서 7장 8~11단계(LED · 배터리 · 모터 · 전압 강하 · 초음파)에 대응합니다.
 *    USB 시리얼 모니터에서 "명령 글자 + Enter" 를 입력하면 됩니다. (115200 bps)
 *
 *  올리기 / 보기 (프로젝트 폴더에서)
 *    pio run -e car1_test -t upload
 *    pio device monitor -e car1_test
 *    (VS Code 에서는 PlatformIO 아이콘 → car1_test → Upload / Monitor)
 *
 *  ※ 안전 수칙
 *    - USB 로 올릴 때는 "5V 레일 → ESP32 5V 핀" 선(B4)을 뽑으세요. (USB 와 5V 동시 공급 금지)
 *      배터리 스위치를 켠 채 USB 를 꽂아 모터를 시험하는 것은 B4 를 뽑은 상태에서만 하세요.
 *    - 모터 시험(m · n · d)은 바퀴를 띄운 채로, 정지 시험(s)은 바퀴를 손으로 잡고 하세요.
 *    - 모터가 돌아가는 동안 아무 글자나 입력하고 Enter 를 누르면 즉시 멈춥니다. (x 도 같음)
 *    - 이 프로그램은 모터를 항상 부드럽게 시작(0.2초)하고, 듀티를 70%(178/255)로 제한합니다.
 *
 *  명령 (설계서 7장 단계)
 *    h        도움말                                       p   배선 확인: 핀을 하나씩 HIGH (멀티미터)
 *    a        자동 점검: 배터리 · 초음파 · LED              l   LED 시험                 [8단계]
 *    b        배터리 측정                       [8단계]     c V 배터리 보정: 멀티미터 값 입력 (예: c 7.52)
 *    u        초음파 거리 (연속, Enter 로 끝)    [11단계]    m   모터 방향 시험 (바퀴 띄움) [9단계]
 *    n        최소 듀티 찾기 (바퀴 띄움)                    d   주행 동작 데모 (바퀴 띄움)
 *    s        정지 시험: 전압 강하 (바퀴 잡음)   [10단계]    i   설정 보기 / "i reset" 초기화
 *    x        모터 즉시 정지
 *
 *  쓰는 핀은 설계서 5장의 S1~S13 과 같습니다. 핀을 바꿨다면 아래 "핀" 부분만 고치세요.
 * =====================================================================
 */
#include <Arduino.h>
#include <Preferences.h>
#include <esp_system.h>

#define CAR1_TEST_VERSION "v1.1 (2026-10-14)"   // v1.1: LED 를 저항 내장 모듈 기준으로 안내 문구만 수정 (동작 변경 없음)

// ============================ 핀 (설계서 5장 S1~S13) ============================
constexpr uint8_t PIN_PWMA  = 25;  // S1  TB6612 PWMA  왼쪽 모터 속도
constexpr uint8_t PIN_AIN1  = 26;  // S2  TB6612 AIN1  왼쪽 방향
constexpr uint8_t PIN_AIN2  = 27;  // S3  TB6612 AIN2  왼쪽 방향
constexpr uint8_t PIN_PWMB  = 13;  // S4  TB6612 PWMB  오른쪽 모터 속도
constexpr uint8_t PIN_BIN1  = 32;  // S5  TB6612 BIN1  오른쪽 방향
constexpr uint8_t PIN_BIN2  = 33;  // S6  TB6612 BIN2  오른쪽 방향
constexpr uint8_t PIN_STBY  = 4;   // S7  TB6612 STBY  (HIGH 여야 모터가 돈다)
constexpr uint8_t PIN_TRIG  = 18;  // S8  초음파 Trig
constexpr uint8_t PIN_ECHO  = 19;  // S9  초음파 Echo (1kΩ/2kΩ 분압 뒤)
constexpr uint8_t PIN_LED_R = 21;  // S10 LED 모듈 R 핀 (빨강). 저항은 모듈 기판에 내장 — 외부 저항 없이 바로 연결
constexpr uint8_t PIN_LED_G = 22;  // S11 LED 모듈 G 핀 (초록)
constexpr uint8_t PIN_LED_B = 23;  // S12 LED 모듈 B 핀 (파랑)   ※ 모듈의 − (GND) 핀은 ESP32 GND 에 연결
constexpr uint8_t PIN_VBAT  = 34;  // S13 배터리 측정 (100kΩ/33kΩ 분압, ADC1)

// ============================ PWM · 모터 설정 ============================
constexpr uint32_t MOTOR_PWM_HZ = 20000;   // 20kHz : 귀에 안 들림 (설계서 6장)
constexpr uint32_t LED_PWM_HZ   = 5000;
constexpr uint8_t  PWM_BITS     = 8;       // 듀티 0~255
constexpr uint8_t  CH_MOTOR_A = 0, CH_MOTOR_B = 1;           // 같은 타이머를 써서 둘 다 20kHz
constexpr uint8_t  CH_LED_R = 2, CH_LED_G = 3, CH_LED_B = 4;

constexpr int      DUTY_MAX  = 178;        // 70% : 모터에 6V 넘게 안 주는 상한 (설계서 3장 ①)
constexpr int      DUTY_TEST = 130;        // 방향·동작 시험용 (약 51%)
constexpr float    TURN_INNER_RATIO = 0.4f;// 회전 때 안쪽 바퀴 = 바깥 바퀴의 40% (설계서 6장)
constexpr uint32_t RAMP_MS = 200;          // 소프트 스타트 시간 (설계서 6장)

constexpr bool     LED_COMMON_ANODE = false;   // LED 모듈의 공통 핀이 + (공통 애노드형)이면 true, − (공통 캐소드형)이면 false
constexpr uint8_t  LED_LEVEL = 80;             // LED 밝기 0~255 (너무 눈부시지 않게. 3.3V 라 초록 · 파랑은 빨강보다 어둡게 보임)

// ============================ 배터리 측정 설정 ============================
constexpr float R_TOP_OHM = 100000.0f, R_BOTTOM_OHM = 33000.0f;                    // 설계서 3장 ③
constexpr float VBAT_K_DEFAULT = (R_TOP_OHM + R_BOTTOM_OHM) / R_BOTTOM_OHM;        // 약 4.03
constexpr float V_FULL = 8.4f, V_WARN = 6.4f, V_STOP = 6.0f;

constexpr uint32_t ECHO_TIMEOUT_US = 12000;    // 약 2m 까지만 기다림 (설계서 6장)

// ============================ 전역 상태 ============================
Preferences prefs;                 // 보정값 · 방향 설정을 껐다 켜도 기억 (NVS)
float vbatK = VBAT_K_DEFAULT;      // 배터리 전압 = 34번 핀 전압 × vbatK
int   minDutyL = 0, minDutyR = 0;  // n 명령으로 찾은 최소 듀티 (0 = 아직 모름)

struct Motor { uint8_t in1, in2, ch; bool invert; const char* name; };
Motor motorL{PIN_AIN1, PIN_AIN2, CH_MOTOR_A, false, "왼쪽"};
Motor motorR{PIN_BIN1, PIN_BIN2, CH_MOTOR_B, false, "오른쪽"};

// ============================ LED ============================
void ledColor(uint8_t r, uint8_t g, uint8_t b) {
  if (LED_COMMON_ANODE) { r = 255 - r; g = 255 - g; b = 255 - b; }
  ledcWrite(CH_LED_R, r);
  ledcWrite(CH_LED_G, g);
  ledcWrite(CH_LED_B, b);
}

// ============================ 핀 준비 ============================
void setupPins() {
  // 무엇보다 먼저 모터가 절대 돌지 않게: STBY = LOW, 방향 핀 = LOW
  pinMode(PIN_STBY, OUTPUT);
  digitalWrite(PIN_STBY, LOW);
  const uint8_t dirPins[] = {PIN_AIN1, PIN_AIN2, PIN_BIN1, PIN_BIN2};
  for (uint8_t p : dirPins) { pinMode(p, OUTPUT); digitalWrite(p, LOW); }

  // 모터 속도 PWM (코어 2.0.17 방식: ledcSetup → ledcAttachPin → ledcWrite(채널))
  ledcSetup(CH_MOTOR_A, MOTOR_PWM_HZ, PWM_BITS);  ledcAttachPin(PIN_PWMA, CH_MOTOR_A);  ledcWrite(CH_MOTOR_A, 0);
  ledcSetup(CH_MOTOR_B, MOTOR_PWM_HZ, PWM_BITS);  ledcAttachPin(PIN_PWMB, CH_MOTOR_B);  ledcWrite(CH_MOTOR_B, 0);

  // LED PWM (밝기 조절)
  ledcSetup(CH_LED_R, LED_PWM_HZ, PWM_BITS);  ledcAttachPin(PIN_LED_R, CH_LED_R);
  ledcSetup(CH_LED_G, LED_PWM_HZ, PWM_BITS);  ledcAttachPin(PIN_LED_G, CH_LED_G);
  ledcSetup(CH_LED_B, LED_PWM_HZ, PWM_BITS);  ledcAttachPin(PIN_LED_B, CH_LED_B);
  ledColor(0, 0, 0);

  pinMode(PIN_TRIG, OUTPUT);  digitalWrite(PIN_TRIG, LOW);
  pinMode(PIN_ECHO, INPUT);

  analogSetPinAttenuation(PIN_VBAT, ADC_11db);   // 권장 측정 범위 0.15~2.45V (설계서 3장 ③)
}

// ============================ 설정 저장 · 불러오기 ============================
void loadSettings() {
  prefs.begin("car1", false);
  vbatK         = prefs.getFloat("k", VBAT_K_DEFAULT);
  motorL.invert = prefs.getBool("invL", false);
  motorR.invert = prefs.getBool("invR", false);
  minDutyL      = prefs.getInt("minL", 0);
  minDutyR      = prefs.getInt("minR", 0);
  prefs.end();
}

void saveFloat(const char* key, float v) { prefs.begin("car1", false); prefs.putFloat(key, v); prefs.end(); }
void saveBool(const char* key, bool v)   { prefs.begin("car1", false); prefs.putBool(key, v);  prefs.end(); }
void saveInt(const char* key, int v)     { prefs.begin("car1", false); prefs.putInt(key, v);   prefs.end(); }

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
// speed : -DUTY_MAX ~ +DUTY_MAX  (+ 는 차의 앞쪽으로 굴러가는 방향, 0 은 힘 빼고 굴러감)
void motorSet(const Motor& m, int speed) {
  speed = constrain(speed, -DUTY_MAX, DUTY_MAX);
  if (speed == 0) {                              // IN1 = IN2 = LOW → coast
    digitalWrite(m.in1, LOW);
    digitalWrite(m.in2, LOW);
    ledcWrite(m.ch, 0);
    return;
  }
  bool forward = speed > 0;
  if (m.invert) forward = !forward;              // 모터가 거울 방향으로 달려 있으면 여기서 뒤집음
  digitalWrite(m.in1, forward ? HIGH : LOW);     // IN1 = H, IN2 = L → 정방향 / 반대 → 역방향
  digitalWrite(m.in2, forward ? LOW : HIGH);
  ledcWrite(m.ch, abs(speed));
}

void allStop() {
  motorSet(motorL, 0);
  motorSet(motorR, 0);
  digitalWrite(PIN_STBY, LOW);
}

// 왼쪽 · 오른쪽 목표 속도로 rampMs 동안 서서히 올리고 holdMs 유지한 뒤 정지.
// 도중에 입력이 들어오면 즉시 정지하고 false.
bool driveFor(int left, int right, uint32_t holdMs, uint32_t rampMs = RAMP_MS) {
  flushInput();                                   // 남은 글자가 "정지 신호"로 오해되지 않게
  digitalWrite(PIN_STBY, HIGH);
  uint32_t t0 = millis();
  while (true) {
    uint32_t t = millis() - t0;
    if (t >= rampMs + holdMs) break;
    float f = (t < rampMs) ? (float)t / (float)rampMs : 1.0f;
    motorSet(motorL, (int)(left * f));
    motorSet(motorR, (int)(right * f));
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
// 34번 핀 전압 (V) — 보드의 공장 교정값을 쓰는 analogReadMilliVolts 를 16번 평균
float readVpin() {
  uint32_t sum = 0;
  for (int i = 0; i < 16; i++) { sum += analogReadMilliVolts(PIN_VBAT); delayMicroseconds(200); }
  return sum / 16.0f / 1000.0f;
}
float readVbat() { return readVpin() * vbatK; }

const char* batteryStatus(float v) {
  if (v < 3.0f)         return "배터리 꺼짐 또는 연결 안 됨 (USB 만 연결?)";
  if (v < V_STOP)       return "사용 중지 수준 (6.0V 이하) — 충전하세요";
  if (v < V_WARN)       return "경고 (6.4V 이하) — 곧 충전";
  if (v <= V_FULL+0.2f) return "정상";
  return "너무 높게 읽힘 — 보정(c)이나 분압 저항 확인";
}

// 모터 시험을 해도 되는 상태인지 확인
bool batteryOkForMotors() {
  float v = readVbat();
  if (v < 3.0f) {
    Serial.printf("[경고] 배터리가 %.2f V 로 읽힙니다 (0V 근처).\n"
                  "  → 배터리 스위치가 꺼졌거나, 34번 핀 쪽 가는 선(분기점 → 100kΩ → GPIO34)이 빠졌을 수 있어요.\n", v);
    return askYesNo("그래도 모터를 시험할까요? (배터리 스위치는 켜져 있어야 합니다)");
  }
  if (v < V_STOP) {
    Serial.printf("[중지] 배터리 %.2f V — %.1f V 이하라 모터 시험을 하지 않습니다. 충전하세요.\n", v, V_STOP);
    return false;
  }
  if (v > V_FULL + 0.3f) {
    Serial.printf("[경고] 배터리 %.2f V 는 만충(8.4V)보다 높게 읽힙니다. 보정(c)이나 분압 저항(33kΩ)을 확인하세요.\n", v);
    return askYesNo("그래도 진행할까요?");
  }
  return true;
}

// ============================ 초음파 ============================
// 거리(cm). 에코가 안 돌아오면 -1 (앞이 트였거나 약 2m 밖)
float readDistanceCm() {
  digitalWrite(PIN_TRIG, LOW);   delayMicroseconds(4);
  digitalWrite(PIN_TRIG, HIGH);  delayMicroseconds(10);          // 트리거 10µs
  digitalWrite(PIN_TRIG, LOW);
  uint32_t us = pulseIn(PIN_ECHO, HIGH, ECHO_TIMEOUT_US);
  if (us == 0) return -1.0f;
  return us / 58.0f;                                              // 거리(cm) = µs ÷ 58
}

// ============================ 명령들 ============================
void printMenu() {
  Serial.println();
  Serial.println("================ 1호차 점검 메뉴 (명령 글자 + Enter) ================");
  Serial.println(" h  도움말                    p  배선 확인: 핀을 하나씩 HIGH (멀티미터)");
  Serial.println(" a  자동 점검 (배터리·초음파·LED)   l  LED 시험                      [8단계]");
  Serial.println(" b  배터리 측정               [8단계]  c V 보정: 멀티미터 값 입력 (예: c 7.52)");
  Serial.println(" u  초음파 거리 (Enter 로 끝)  [11단계] m  모터 방향 시험 (바퀴 띄움)    [9단계]");
  Serial.println(" n  최소 듀티 찾기 (바퀴 띄움)         d  주행 동작 데모 (바퀴 띄움)");
  Serial.println(" s  정지 시험: 전압 강하 (바퀴 잡음)  [10단계]  i  설정 보기 / i reset 초기화");
  Serial.println(" x  모터 즉시 정지            ※ 모터가 도는 중 아무 글자 + Enter = 즉시 정지");
  Serial.println("======================================================================");
}

void cmdLed() {
  Serial.println("\n[LED 시험] 빨강 → 초록 → 파랑 → 흰색 → 꺼짐 (각 0.7초)");
  struct ColorStep { const char* name; uint8_t r, g, b; };
  const ColorStep steps[] = {
    {"빨강", LED_LEVEL, 0, 0}, {"초록", 0, LED_LEVEL, 0}, {"파랑", 0, 0, LED_LEVEL},
    {"흰색", LED_LEVEL, LED_LEVEL, LED_LEVEL},
  };
  for (const ColorStep& s : steps) {
    Serial.printf("  %s\n", s.name);
    ledColor(s.r, s.g, s.b);
    if (waitOrAbort(700)) break;
  }
  ledColor(0, 0, 0);
  Serial.println("  → 색이 하나라도 안 켜지면 그 색의 핀 이름(R · G · B)과 점퍼선 연결을 확인하세요. (초록 · 파랑이 빨강보다 어두운 것은 3.3V 구동이라 정상)");
  Serial.println("  → 아무 색도 안 켜지면 LED 모듈이 공통 애노드형(공통 핀이 +)일 수 있어요: 그 핀을 GND 대신 3V3 에 잇고, 파일 위쪽 LED_COMMON_ANODE 를 true 로 바꿔 다시 올리세요.");
}

void cmdBattery() {
  Serial.println("\n[배터리 측정] 2초 동안 10번 읽습니다.");
  float sum = 0, vmin = 99.0f, vmax = 0.0f;
  int n = 0;
  for (int i = 0; i < 10; i++) {
    float vp = readVpin();
    float vb = vp * vbatK;
    sum += vb;  n++;
    vmin = min(vmin, vb);  vmax = max(vmax, vb);
    Serial.printf("  34번 핀 %.3f V  →  배터리 %.2f V\n", vp, vb);
    if (waitOrAbort(200)) break;
  }
  if (n == 0) return;
  float avg = sum / n;
  Serial.printf("평균 %.2f V  (최소 %.2f ~ 최대 %.2f)  →  %s\n", avg, vmin, vmax, batteryStatus(avg));
  Serial.printf("보정계수 %.3f  (기본 %.3f)\n", vbatK, VBAT_K_DEFAULT);
  if (avg >= V_STOP) {
    float limit = min(255.0f, 255.0f * 6.0f / avg);
    Serial.printf("참고: 모터에 6.0V 를 넘지 않게 하려면 최대 듀티 ≈ %.0f / 255 (이 프로그램은 %d 로 고정)\n", limit, DUTY_MAX);
  }
  Serial.println("→ 멀티미터로 배터리 전압을 재서 ±0.2V 이내면 합격. 다르면 \"c 멀티미터값\" 으로 보정하세요.");
}

void cmdCalibrate(const String& arg) {
  if (arg.equalsIgnoreCase("reset")) {
    vbatK = VBAT_K_DEFAULT;
    saveFloat("k", vbatK);
    Serial.printf("보정계수를 기본값 %.3f 로 되돌렸습니다.\n", vbatK);
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
    Serial.println("34번 핀 전압이 너무 낮아요 (배터리 꺼짐 / 분압 선 확인). 보정하지 않았습니다.");
    return;
  }
  float k = measured / vp;
  if (k < 3.3f || k > 4.8f) {
    Serial.printf("계산된 보정계수 %.3f 가 기본값(%.3f)과 너무 달라요. 100kΩ/33kΩ 저항 값과 배선을 먼저 확인하세요.\n", k, VBAT_K_DEFAULT);
    return;
  }
  vbatK = k;
  saveFloat("k", vbatK);
  Serial.printf("보정 완료: 보정계수 %.3f (저장됨). 이제 배터리 = %.2f V 로 읽힙니다. (b 로 다시 확인)\n", vbatK, readVbat());
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
    Serial.println("  → 계속 미검출이면: Trig=GPIO18 / Echo(분압 뒤)=GPIO19, 센서 VCC 5V · GND, 1kΩ/2kΩ 분압을 확인하세요.");
  }
}

void cmdDirection() {
  Serial.println("\n[모터 방향 시험]  ※ 바퀴를 바닥에서 띄우세요 (차를 들거나 받침 위에)");
  Serial.println("  (왼쪽 · 오른쪽 = 차를 뒤에서 앞쪽으로 바라봤을 때의 왼쪽 · 오른쪽)");
  if (!batteryOkForMotors()) return;
  if (!askYesNo("바퀴가 떠 있고 배터리 스위치가 켜져 있나요?")) return;
  bool changed = false;
  Motor* motors[] = {&motorL, &motorR};
  for (Motor* m : motors) {
    bool isLeft = (m == &motorL);
    Serial.printf("\n--- %s 모터: 전진 1.5초 (이 바퀴만) ---\n", m->name);
    Serial.println("  차의 '앞쪽'으로 굴러가는 방향이면 정상입니다. (중간에 아무 글자 + Enter = 즉시 정지)");
    if (!(isLeft ? driveFor(DUTY_TEST, 0, 1500) : driveFor(0, DUTY_TEST, 1500))) return;
    delay(300);
    int c = askChoice("결과는?  1=앞으로 돌았다  2=뒤로(반대) 돌았다  3=안 돌았다", 3);
    if (c == 2) {
      m->invert = !m->invert;
      saveBool(isLeft ? "invL" : "invR", m->invert);
      changed = true;
      Serial.printf("  → %s 모터 방향 반전을 %s 로 저장했습니다. (모터 선 두 개를 바꿔 납땜해도 됩니다)\n", m->name, m->invert ? "켬" : "끔");
    } else if (c == 3) {
      Serial.println("  → 안 돌면 확인: ① STBY(GPIO4)가 HIGH 인가 (p 명령으로 핀 확인) ② TB6612 VM 에 배터리 전압이 오는가 ③ GND 연결");
      Serial.println("                  ④ 그 모터의 AO/BO 납땜과 PWM · IN1 · IN2 점퍼선 ⑤ 듀티가 너무 낮은 건 아닌지 (n 명령)");
      continue;
    } else if (c == 0) {
      Serial.println("  입력이 없어 이 모터는 건너뜁니다.");
      continue;
    }
    // 후진도 확인 (IN1/IN2 두 선이 모두 맞게 연결됐는지)
    Serial.printf("--- %s 모터: 후진 1.5초 ---\n", m->name);
    if (!(isLeft ? driveFor(-DUTY_TEST, 0, 1500) : driveFor(0, -DUTY_TEST, 1500))) return;
    delay(300);
    if (!askYesNo("  전진과 반대 방향으로 돌았나요?")) {
      Serial.println("  → 후진이 안 되면 그 모터의 IN2(AIN2 또는 BIN2) 선이나 IN1 선 연결을 확인하세요.");
    }
  }
  allStop();
  Serial.println(changed ? "\n방향 설정이 바뀌었습니다. m 으로 다시 한 번 확인하세요." : "\n방향 시험 끝.");
}

void cmdMinDuty() {
  Serial.println("\n[최소 듀티 찾기]  ※ 바퀴를 띄우세요.");
  Serial.println("  듀티를 30부터 5씩 올립니다. 바퀴가 '돌기 시작하는 순간' Enter 를 치세요. (왼쪽 → 오른쪽 순서)");
  if (!batteryOkForMotors()) return;
  if (!askYesNo("바퀴가 떠 있고 배터리 스위치가 켜져 있나요?")) return;
  Motor* motors[] = {&motorL, &motorR};
  for (Motor* m : motors) {
    bool isLeft = (m == &motorL);
    Serial.printf("\n--- %s 모터 --- (돌기 시작하면 Enter)\n", m->name);
    digitalWrite(PIN_STBY, HIGH);
    int found = -1;
    for (int duty = 30; duty <= 150; duty += 5) {
      motorSet(*m, duty);
      Serial.printf("  듀티 %d (%.0f%%)\n", duty, duty * 100.0f / 255.0f);
      if (waitOrAbort(600)) { found = duty; break; }
    }
    allStop();
    if (found < 0) {
      Serial.println("  150 까지 올려도 안 돌았다면 배선 · 전원을 먼저 확인하세요 (m 명령).");
      continue;
    }
    Serial.printf("  → %s 모터가 %d 부터 돌기 시작. 안전하게 쓰려면 최소 듀티를 약 %d 로 잡으세요 (여유 +10).\n", m->name, found, found + 10);
    if (isLeft) { minDutyL = found + 10; saveInt("minL", minDutyL); }
    else        { minDutyR = found + 10; saveInt("minR", minDutyR); }
    delay(400);
  }
  Serial.println("\n(설계서 6장의 '최소 듀티 90' 은 이 값으로 바꾸세요. 저장해 두었으니 i 로 볼 수 있어요.)");
}

void cmdDrive() {
  Serial.println("\n[주행 동작 데모]  ※ 바퀴를 띄우세요. 각 동작 후 정지합니다. (아무 글자 + Enter = 중단)");
  if (!batteryOkForMotors()) return;
  if (!askYesNo("바퀴가 떠 있고 배터리 스위치가 켜져 있나요?")) return;
  struct Move { const char* name; int l, r; uint32_t ms; const char* expect; };
  const int T = DUTY_TEST;
  const int inner = (int)(DUTY_MAX * TURN_INNER_RATIO);   // 안쪽 바퀴 = 바깥(최대)의 40%
  const Move moves[] = {
    {"전진",          T,  T,  2000, "두 바퀴 모두 앞으로"},
    {"후진",         -T, -T,  2000, "두 바퀴 모두 뒤로"},
    {"좌회전",    inner, DUTY_MAX, 2000, "왼쪽 바퀴가 오른쪽보다 느리게 (최소 듀티보다 낮으면 멈춰 있을 수 있음)"},
    {"우회전", DUTY_MAX, inner, 2000, "오른쪽 바퀴가 왼쪽보다 느리게 (최소 듀티보다 낮으면 멈춰 있을 수 있음)"},
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

// 정지(스톨) 시험 — 설계서 7장 10단계 "전압 강하 시험"
void cmdStall() {
  Serial.println("\n[정지 시험: 전압 강하]  ※ 두 바퀴를 손으로 꽉 잡아 못 돌게 하세요. 모터는 딱 1초만 돕니다.");
  Serial.println("  1) 멀티미터(DC V)를 '배터리 홀더의 + 단자' 와 'TB6612 VM 핀' 사이에 댑니다.");
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
  digitalWrite(PIN_STBY, HIGH);
  uint32_t t0 = millis();
  Serial.println("  >>> 구동 중 (1초)");
  while (millis() - t0 < 1000) {
    float f = min(1.0f, (millis() - t0) / 100.0f);          // 0.1초 소프트 스타트 후 70% 유지
    motorSet(motorL, (int)(DUTY_MAX * f));
    motorSet(motorR, (int)(DUTY_MAX * f));
    float v = readVbat();
    if (v < vmin) vmin = v;
    delay(15);
  }
  allStop();
  delay(400);
  float after = readVbat();
  Serial.println("  <<< 정지");
  Serial.printf("배터리(34번 핀 기준): 시험 전 %.2f V → 구동 중 최저 %.2f V (처짐 %.2f V) → 정지 후 %.2f V\n", before, vmin, before - vmin, after);
  Serial.println("  ※ 이 값은 분기점에서 잰 배터리 쪽 처짐입니다. 합격 판정은 멀티미터로 본 '배터리 + ↔ VM' 값(1.0V 이하)으로 하세요.");
  Serial.println("  → 1.0V 를 넘으면: 납땜 불량 · 접촉 불량 · 가는 전선 · 스위치(AC 정격만 있는 KCD1 등)를 의심하세요.");
  Serial.println("  → 시험 직후 TB6612 칩 · 스위치 · 퓨즈 홀더가 뜨겁지 않은지 손으로 확인하세요.");
}

// 배선 확인 — 핀을 하나씩 HIGH 로 만들고 멀티미터로 해당 핀이 3.3V 인지 확인
void cmdPins() {
  struct PinStep { const char* gpio; const char* target; uint8_t pin; int8_t ch; };
  const PinStep list[] = {
    {"GPIO25", "TB6612 PWMA (왼쪽 속도)", PIN_PWMA, CH_MOTOR_A},
    {"GPIO26", "TB6612 AIN1 (왼쪽 방향)", PIN_AIN1, -1},
    {"GPIO27", "TB6612 AIN2 (왼쪽 방향)", PIN_AIN2, -1},
    {"GPIO13", "TB6612 PWMB (오른쪽 속도)", PIN_PWMB, CH_MOTOR_B},
    {"GPIO32", "TB6612 BIN1 (오른쪽 방향)", PIN_BIN1, -1},
    {"GPIO33", "TB6612 BIN2 (오른쪽 방향)", PIN_BIN2, -1},
    {"GPIO4",  "TB6612 STBY", PIN_STBY, -1},
    {"GPIO18", "초음파 Trig", PIN_TRIG, -1},
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
  Serial.println("\n끝. 이 프로그램이 못 보는 전원 3가지는 멀티미터로 직접 확인하세요:");
  Serial.println("  · TB6612 VCC = 3.3V   · TB6612 VM = 배터리 전압(스위치 ON)   · 브레드보드 5V 레일 = 5.0V");
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
    prefs.begin("car1", false);
    prefs.clear();
    prefs.end();
    vbatK = VBAT_K_DEFAULT;
    motorL.invert = motorR.invert = false;
    minDutyL = minDutyR = 0;
    Serial.println("저장된 설정(보정계수 · 방향 반전 · 최소 듀티)을 모두 지웠습니다.");
    return;
  }
  Serial.println("\n[설정]  (i reset = 초기화)");
  Serial.printf("  프로그램 버전      : %s\n", CAR1_TEST_VERSION);
  Serial.printf("  배터리 보정계수    : %.3f  (기본 %.3f)\n", vbatK, VBAT_K_DEFAULT);
  Serial.printf("  왼쪽 방향 반전     : %s\n", motorL.invert ? "켬" : "끔");
  Serial.printf("  오른쪽 방향 반전   : %s\n", motorR.invert ? "켬" : "끔");
  Serial.printf("  최소 듀티 (왼/오)  : %d / %d  (0 = 아직 모름, n 으로 찾기)\n", minDutyL, minDutyR);
  Serial.printf("  최대 듀티          : %d / 255 (70%%), PWM %lu Hz, 소프트 스타트 %lu ms\n", DUTY_MAX, (unsigned long)MOTOR_PWM_HZ, (unsigned long)RAMP_MS);
  Serial.printf("  LED                : %s, 밝기 %d\n", LED_COMMON_ANODE ? "공통 애노드" : "공통 캐소드", LED_LEVEL);
}

// ============================ Arduino ============================
void setup() {
  setupPins();                  // 가장 먼저: 모터가 돌지 않게 STBY = LOW
  Serial.begin(115200);
  delay(300);
  loadSettings();
  Serial.println();
  Serial.println("##################################################");
  Serial.printf ("#  1호차 하드웨어 점검 프로그램  %s\n", CAR1_TEST_VERSION);
  Serial.println("##################################################");
  Serial.printf ("직전 리셋 원인: %s\n", resetReasonName(esp_reset_reason()));
  float v = readVbat();
  Serial.printf ("배터리: %.2f V  (%s)\n", v, batteryStatus(v));
  Serial.println("※ 모터 시험은 바퀴를 띄우고, USB 를 꽂았다면 B4(5V 선)를 뽑은 상태에서 하세요.");
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
    case 'h': case '?': printMenu();       break;
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
    case 'i': cmdInfo(arg);                break;
    case 'x': allStop(); ledColor(0, 0, 0); Serial.println("모터 정지."); break;
    default:  Serial.printf("모르는 명령 '%s' — h 를 입력하면 메뉴가 나옵니다.\n", line.c_str()); break;
  }
  Serial.println("\n(명령을 입력하세요. h = 메뉴)");
}

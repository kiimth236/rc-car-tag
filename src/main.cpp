/*
 * RC카 본 프로그램 — 폰 웹 조종(Wi-Fi AP + WebSocket) + ESP-NOW 술래잡기
 * 폰: Wi-Fi RCAR-<차 번호> (비밀번호 rcartag<차 번호>) 에 붙고 브라우저에서 http://192.168.4.1
 * 차 번호 · 역할 · 보정값은 점검 프로그램(car1_test)의 r · m · n · c · k 로 먼저 저장하세요.
 * 도주차는 자석이 없는 곳에서 켜세요 (켤 때 1초 동안 홀 센서 기준값을 잽니다).
 */
#include <Arduino.h>
#include <esp_system.h>
#include <math.h>
#include <string.h>
#include "analog_inputs.h"
#include "battery_monitor.h"
#include "drive.h"
#include "drive_label.h"
#include "drive_math.h"
#include "game.h"
#include "hall_detector.h"
#include "led_patterns.h"
#include "link_watch.h"
#include "motor_l298n.h"
#include "radio.h"
#include "rcar_config.h"
#include "rgb_led.h"
#include "settings.h"
#include "sonar.h"
#include "web_control.h"
#include "web_proto.h"

static CarSettings S;
static L298NMotor motorL(cfg::PIN_ENA, cfg::PIN_IN1, cfg::PIN_IN2, cfg::CH_MOTOR_L);
static L298NMotor motorR(cfg::PIN_ENB, cfg::PIN_IN3, cfg::PIN_IN4, cfg::CH_MOTOR_R);
static Drive drive(motorL, motorR);
static rcar::BatteryMonitor battery;
static uint8_t battDeciV = 0;
static float sonarHist[3] = {-1, -1, -1};
static uint8_t sonarIdx = 0;
static float frontCm = -1;  // −1 = 미검출 또는 안 쏨
static uint32_t nextControl = 0, nextSonar = 0, nextBattery = 0, nextLed = 0, nextLog = 0, nextHall = 0, nextUi = 0;
static rcar::HallDetector hall;
static rcar::Game* game = nullptr;  // 항상 만듦 (차 번호 0 이어도 화면·혼자 놀기용). 무선 게임은 1~4번만
static bool radioOk = false;        // Wi-Fi AP · ESP-NOW 시작됨
static bool espNow = false;         // ESP-NOW 로 게임 메시지를 주고받음 (radioOk && 차 번호 1~4)
static rcar::Phase prevPhase = rcar::Phase::Idle;

// 폰 입력: 조이스틱 J 가 0.5초 안 오면 끊김으로 봄
static rcar::LinkWatch pad(cfg::PAD_RX_TIMEOUT_MS);
static int8_t joyX = 0, joyY = 0;  // 마지막 조이스틱 (−100~100)
static bool everConnected = false;
static uint32_t lastConnected = 0;  // 마지막으로 "연결"이었던 시각 (phoneOk 유예용)

static constexpr size_t kStateJsonCap = 640;  // web_control.cpp 의 kJsonCap 과 같게

static bool due(uint32_t now, uint32_t& next, uint32_t period) {
  if ((int32_t)(now - next) < 0) return false;
  next = now + period;
  return true;
}

static void blinkBoot(rcar::Rgb c, int times) {
  for (int i = 0; i < times; i++) { ledWrite(c); delay(150); ledWrite({0, 0, 0}); delay(150); }
}

// 시리얼 로그용 모드 이름
static const char* modeName(rcar::UiMode m) {
  switch (m) {
    case rcar::UiMode::Menu: return "\xEB\xA9\x94\xEB\x89\xB4";                                       // "메뉴"
    case rcar::UiMode::Lobby: return "\xEA\xB2\x8C\xEC\x9E\x84";                                      // "게임"
    case rcar::UiMode::Solo: return "\xED\x98\xBC\xEC\x9E\x90 \xEB\x86\x80\xEA\xB8\xB0";              // "혼자 놀기"
    case rcar::UiMode::Rename: return "\xEC\x9D\xB4\xEB\xA6\x84 \xEB\xB0\x94\xEA\xBE\xB8\xEA\xB8\xB0";  // "이름 바꾸기"
  }
  return "?";
}

// 폰 명령 하나를 게임·주행 입력에 반영
static void handleCmd(rcar::Game& g, const rcar::WebCmd& c, uint32_t now) {
  using K = rcar::WebCmd::Kind;
  switch (c.kind) {
    case K::Joy:
      pad.onRx(now);
      joyX = c.x;
      joyY = c.y;
      break;
    case K::Select:
      g.onSelect(now);
      break;
    case K::Start:
      if (g.mode() != rcar::UiMode::Menu) g.onStartPressed(now);  // 메뉴 고르기는 페이지가 M 으로 보냄
      break;
    case K::Mode:
      g.setMode(c.mode);  // 참가·관전 중에는 Game 이 거부
      break;
    case K::Time: {
      rcar::Phase ph = g.phase();
      if (S.role == rcar::Role::Chaser && g.mode() == rcar::UiMode::Lobby &&
          (ph == rcar::Phase::Idle || ph == rcar::Phase::Over)) {
        g.setGameSeconds(rcar::stepGameSeconds(g.gameSeconds(), c.dir));
        if (g.gameSeconds() != S.gameSec) {  // 추격차만 기억 (도주차는 추격차 값을 따름)
          S.gameSec = g.gameSeconds();
          settingsSave(S);
        }
      }
      break;
    }
    case K::Toggle:
      g.toggleParticipant(c.id, now);
      break;
    case K::Name:
      if (g.mode() == rcar::UiMode::Rename) {
        g.setName(c.name);  // 이상한 이름은 Game 이 무시 → 저장도 안 됨
        if (strcmp(S.name, g.name()) != 0) {
          strncpy(S.name, g.name(), sizeof S.name - 1);
          S.name[sizeof S.name - 1] = 0;
          settingsSave(S);
        }
        g.setMode(rcar::UiMode::Menu);
      }
      break;
    default:
      break;
  }
}

// 폰에 보낼 상태 JSON 을 만들어 웹 태스크에 넘김
static void publishState(rcar::Game& g, uint32_t now) {
  rcar::UiSnapshot s{};
  s.id = S.carId;
  s.role = S.role;
  s.name = g.name();
  s.mode = g.mode();
  s.phase = g.phase();
  s.spectating = g.spectating();
  s.caughtMe = g.caught();
  s.lock = !g.motorsEnabled(now);
  s.participants = g.participantsMask(now);
  s.caughtMask = g.caughtMask();
  s.eligible = g.eligibleMask(now);
  s.msLeft = g.msLeft(now);
  s.gameSec = g.gameSeconds();
  s.winner = g.winner();
  s.label = rcar::classifyDrive(drive.dutyL(), drive.dutyR(), drive.frontBlocked());
  s.battDeciV = battDeciV;
  s.bs = battery.state();
  s.distCm = frontCm >= 0 ? (int16_t)lroundf(fminf(frontCm, 9999.0f)) : -1;  // 안 쏠 때는 frontCm = −1
  s.names[0] = "";
  for (uint8_t i = 1; i <= 4; i++) s.names[i] = g.peerName(i);  // 내 칸은 Game 이 내 이름을 돌려줌
  static char json[kStateJsonCap];
  if (rcar::buildStateJson(s, json, sizeof json) > 0) webPublish(json);
}

void setup() {
  motorL.begin(cfg::MOTOR_PWM_HZ_DEFAULT);  // 가장 먼저 모터 정지 상태로
  motorR.begin(cfg::MOTOR_PWM_HZ_DEFAULT);
  Serial.begin(115200);
  ledBegin();
  analogInputsBegin();
  if (cfg::SONAR_INSTALLED) sonarBegin();
  S = settingsLoad();
  motorL.begin(S.pwmHz);
  motorR.begin(S.pwmHz);
  motorL.setInvert(S.invertL);
  motorR.setInvert(S.invertR);
  drive.setMinDuty(S.minDutyL, S.minDutyR);

  esp_reset_reason_t rr = esp_reset_reason();
  // "[RC카] 차 %d번 %s · 직전 리셋 원인 %d%s"
  Serial.printf("\n[RC\xEC\xB9\xB4] \xEC\xB0\xA8 %d\xEB\xB2\x88 %s \xC2\xB7 \xEC\xA7\x81\xEC\xA0\x84 \xEB\xA6\xAC\xEC\x85\x8B \xEC\x9B\x90\xEC\x9D\xB8 %d%s\n", S.carId,
                S.role == rcar::Role::Chaser ? "\xEC\xB6\x94\xEA\xB2\xA9" : "\xEB\x8F\x84\xEC\xA3\xBC", (int)rr,
                rr == ESP_RST_BROWNOUT ? " (\xEC\xA0\x84\xEC\x95\x95 \xEB\xB6\x80\xEC\xA1\xB1! \xEB\xB0\xB0\xEC\x84\xA0\xC2\xB7\xEC\xBB\xA4\xED\x8C\xA8\xEC\x8B\x9C\xED\x84\xB0\xC2\xB7\xEB\xB0\xB0\xED\x84\xB0\xEB\xA6\xAC \xED\x99\x95\xEC\x9D\xB8)" : "");
  if (rr == ESP_RST_BROWNOUT) blinkBoot({cfg::LED_LEVEL, 0, cfg::LED_LEVEL}, 3);  // 보라 3번
  // "[주의] 차 번호가 없습니다. car1_test 의 r 명령으로 정하세요 (지금은 혼자 놀기만)."
  if (S.carId == 0) Serial.println("[\xEC\xA3\xBC\xEC\x9D\x98] \xEC\xB0\xA8 \xEB\xB2\x88\xED\x98\xB8\xEA\xB0\x80 \xEC\x97\x86\xEC\x8A\xB5\xEB\x8B\x88\xEB\x8B\xA4. car1_test \xEC\x9D\x98 r \xEB\xAA\x85\xEB\xA0\xB9\xEC\x9C\xBC\xEB\xA1\x9C \xEC\xA0\x95\xED\x95\x98\xEC\x84\xB8\xEC\x9A\x94 (\xEC\xA7\x80\xEA\xB8\x88\xEC\x9D\x80 \xED\x98\xBC\xEC\x9E\x90 \xEB\x86\x80\xEA\xB8\xB0\xEB\xA7\x8C).");

  rcar::GameConfig gc = rcar::kDefaultGameConfig;
  gc.offlineCatchMs = cfg::RUNNER_OFFLINE_CATCH_MS;
  gc.gameSecDefault = cfg::GAME_SEC_DEFAULT;
  game = new rcar::Game(S.role, S.carId, gc);
  game->setName(S.name);
  game->setGameSeconds(S.gameSec);

  if (S.role == rcar::Role::Runner) {
    hall.setThreshold(S.hallThresholdMv);
    float sum = 0;
    for (int i = 0; i < 50; i++) { sum += hallReadMv(); delay(20); }  // 1초 평균 (자석 없는 곳에서 켜기)
    hall.setBaseline(sum / 50);
    // "홀 기준값 %.1f mV (저장값 %.1f mV) · 잡힘 기준 %.1f mV"
    Serial.printf("\xED\x99\x80 \xEA\xB8\xB0\xEC\xA4\x80\xEA\xB0\x92 %.1f mV (\xEC\xA0\x80\xEC\x9E\xA5\xEA\xB0\x92 %.1f mV) \xC2\xB7 \xEC\x9E\xA1\xED\x9E\x98 \xEA\xB8\xB0\xEC\xA4\x80 %.1f mV\n", hall.baseline(), S.hallBase,
                  hall.threshold());
  }
  radioOk = radioBegin(S.carId);  // Wi-Fi AP 는 차 번호와 상관없이 항상 (0번이면 RCAR-0)
  espNow = radioOk && S.carId >= 1 && S.carId <= 4;
  // "Wi-Fi AP·ESP-NOW %s%s"
  Serial.printf("Wi-Fi AP\xC2\xB7" "ESP-NOW %s%s\n", radioOk ? "\xEC\xA4\x80\xEB\xB9\x84\xEB\x90\xA8" : "\xEC\x8B\x9C\xEC\x9E\x91 \xEC\x8B\xA4\xED\x8C\xA8!",
                radioOk && !espNow ? " (\xEC\xB0\xA8 \xEB\xB2\x88\xED\x98\xB8 0: \xEA\xB2\x8C\xEC\x9E\x84 \xEB\xAC\xB4\xEC\x84\xA0 \xEC\x95\x88 \xEC\x94\x80)" : "");
  webBegin();
  // "폰: Wi-Fi RCAR-%u (비밀번호 %s%u) 접속 -> http://192.168.4.1"
  Serial.printf("\xED\x8F\xB0: Wi-Fi RCAR-%u (\xEB\xB9\x84\xEB\xB0\x80\xEB\xB2\x88\xED\x98\xB8 %s%u) \xEC\xA0\x91\xEC\x86\x8D -> http://192.168.4.1\n", S.carId, cfg::AP_PASS_PREFIX, S.carId);
}

void loop() {
  uint32_t now = millis();
  rcar::Game& g = *game;

  // 폰 명령 → 게임 · 조이스틱
  rcar::WebCmd wc;
  while (webPollCmd(wc)) handleCmd(g, wc, now);
  bool connected = pad.update(webClientConnected(), now);
  if (connected) {
    everConnected = true;
    lastConnected = now;
  }
  g.setPhoneOk(everConnected && (uint32_t)(now - lastConnected) < cfg::PHONE_GRACE_MS);  // 잠깐 끊겨도 게임 대기 유지

  // 무선 (차 번호 0 은 받은 것을 버리기만)
  rcar::Message m;
  while (radioOk && radioReceive(m)) {
    if (espNow) g.onMessage(m, now);
  }
  // 진행 중인 판에 재부팅으로 돌아옴 → 켤 때 잰 기준값 대신 대기 중에 저장한 값 (추격차 자석 근처에서 쟀을 수 있음)
  if (g.takeRejoined() && S.role == rcar::Role::Runner && S.hallBase > 0) {
    hall.setBaseline(S.hallBase);
    // "[복귀] 홀 기준값을 저장값 %.1f mV 로"
    Serial.printf("[\xEB\xB3\xB5\xEA\xB7\x80] \xED\x99\x80 \xEA\xB8\xB0\xEC\xA4\x80\xEA\xB0\x92\xEC\x9D\x84 \xEC\xA0\x80\xEC\x9E\xA5\xEA\xB0\x92 %.1f mV \xEB\xA1\x9C\n", S.hallBase);
  }

  if (due(now, nextBattery, 100)) {
    battery.update(batteryPinVolts() * S.vbatK, now);
    battDeciV = (uint8_t)constrain((int)(battery.filtered() * 10.0f + 0.5f), 0, 255);
    g.setBatteryDeciV(battDeciV);
  }

  if (S.role == rcar::Role::Runner && due(now, nextHall, 10)) {
    if (hall.update(hallReadMv(), g.hallArmed())) {
      g.onHallTriggered(now);
      // "[잡힘] 변화량 %.1f mV"
      Serial.printf("[\xEC\x9E\xA1\xED\x9E\x98] \xEB\xB3\x80\xED\x99\x94\xEB\x9F\x89 %.1f mV\n", hall.delta());
    }
  }

  // 초음파: 추격차는 항상. 도주차는 혼자 놀기이고 주변에 진행 중인 판이 없을 때만 (소리가 섞이지 않게)
  // 센서를 달지 않은 구성(cfg::SONAR_INSTALLED = false)에서는 아예 쏘지 않음 → frontCm = −1 ("--", 앞 거리 제한 없음)
  bool sonarOn = cfg::SONAR_INSTALLED &&
                 (S.role == rcar::Role::Chaser || (g.mode() == rcar::UiMode::Solo && !g.roundRunningNearby(now)));
  if (!sonarOn) {
    frontCm = -1;
    sonarHist[0] = sonarHist[1] = sonarHist[2] = -1;  // 다시 쏠 때 옛 값이 섞이지 않게
  } else if (due(now, nextSonar, cfg::SONAR_PERIOD_MS)) {
    sonarHist[sonarIdx] = sonarReadCm();
    sonarIdx = (sonarIdx + 1) % 3;
    frontCm = rcar::median3Distance(sonarHist[0], sonarHist[1], sonarHist[2]);
  }

  g.tick(now);
  while (g.popOutgoing(m)) {
    if (espNow) radioSend(m);
  }
  if (g.takeBrake()) drive.brakeNow(now);

  // 판이 카운트다운으로 들어가는 순간(대기→ · 끝→ 둘 다) 지금 홀 기준값을 저장 (판 도중 재부팅 복귀용)
  rcar::Phase ph = g.phase();
  if (S.role == rcar::Role::Runner && ph == rcar::Phase::Countdown && prevPhase != rcar::Phase::Countdown &&
      fabsf(hall.baseline() - S.hallBase) > 1.0f) {
    S.hallBase = hall.baseline();
    settingsSave(S);
  }
  prevPhase = ph;

  if (due(now, nextControl, cfg::CONTROL_PERIOD_MS)) {
    bool enabled = connected && battery.state() != rcar::BatteryState::Cutoff && g.motorsEnabled(now);
    float throttle = connected ? joyY / 100.0f : 0.0f;
    float steer = connected ? joyX / 100.0f * cfg::STEER_GAIN : 0.0f;
    drive.command(throttle, steer);
    drive.update(now, battery.filtered(), S.role == rcar::Role::Chaser ? frontCm : -1.0f, enabled);  // 앞 거리 제한은 추격차만
  }

  if (due(now, nextLed, 20)) {
    ledWrite(rcar::patternColor(g.led(now), S.role, S.carId, g.countdownMsLeft(now), battery.state(), now,
                                cfg::LED_LEVEL));
  }

  if (due(now, nextUi, cfg::UI_PUSH_MS)) publishState(g, now);

  if (due(now, nextLog, 1000)) {
    // "폰 %s · 모드 %s · 배터리 %.2fV (%d) · 단계 %d · 판 %u · 잡힘 %d · 앞 %.1fcm · 홀Δ %.1fmV"
    Serial.printf("\xED\x8F\xB0 %s \xC2\xB7 \xEB\xAA\xA8\xEB\x93\x9C %s \xC2\xB7 \xEB\xB0\xB0\xED\x84\xB0\xEB\xA6\xAC %.2fV (%d) \xC2\xB7 \xEB\x8B\xA8\xEA\xB3\x84 %d \xC2\xB7 \xED\x8C\x90 %u \xC2\xB7 \xEC\x9E\xA1\xED\x9E\x98 %d \xC2\xB7 \xEC\x95\x9E %.1fcm \xC2\xB7 \xED\x99\x80\xCE\x94 %.1fmV\n",
                  connected ? "\xEC\x97\xB0\xEA\xB2\xB0" : "\xEB\x8C\x80\xEA\xB8\xB0", modeName(g.mode()), battery.filtered(), (int)battery.state(),
                  (int)g.phase(), g.round(), (int)g.caught(), frontCm, hall.delta());
  }
}

// Dabble 헤더를 넣는 파일은 이것 하나뿐이어야 합니다 (모듈 객체가 헤더에서 만들어짐).
#define CUSTOM_SETTINGS
#define INCLUDE_GAMEPAD_MODULE
#include <DabbleESP32.h>
#include <BLEDevice.h>
#include "ble_control.h"
#include "link_watch.h"
#include "rcar_config.h"

// DabbleESP32 1.5.1 의 isAppConnected() 는 한 번 true 가 되면 내려가지 않고, 끊긴 뒤 광고도 다시 시작하지 않는다.
// 그래서 라이브러리의 BLE 연결 플래그(esp32BLEUtilities.cpp 의 전역)와 받은 바이트로 직접 판단한다.
extern bool isDeviceConnected;
static rcar::LinkWatch s_link(cfg::PAD_RX_TIMEOUT_MS);

void bleBegin(const char* name) { Dabble.begin(name); }

PadInput blePoll() {
  static bool prevStart = false;
  uint32_t now = millis();
  if (esp32ble.available()) s_link.onRx(now);  // processInput 이 읽어 가기 전에 "새 입력 있음" 기록
  Dabble.processInput();
  PadInput in{};
  in.connected = s_link.update(isDeviceConnected, now);
  if (s_link.takeReadvertise()) BLEDevice::startAdvertising();  // 끊긴 폰이 다시 찾을 수 있게
  float x = 0, y = 0;
  if (GamePad.getRadius() > 0) {  // 조이스틱 모드: 각 축 -7 ~ 7
    x = GamePad.getXaxisData() / 7.0f;
    y = GamePad.getYaxisData() / 7.0f;
  } else {                        // 방향키 모드
    if (GamePad.isUpPressed()) y = 1;
    if (GamePad.isDownPressed()) y = -1;
    if (GamePad.isLeftPressed()) x = -1;
    if (GamePad.isRightPressed()) x = 1;
  }
  if (cfg::PAD_INVERT_Y) y = -y;
  in.throttle = y;
  in.steer = x * cfg::STEER_GAIN;
  bool s = GamePad.isStartPressed();
  in.startEdge = s && !prevStart && in.connected;
  prevStart = s;
  if (!in.connected) { in.throttle = 0; in.steer = 0; }
  return in;
}

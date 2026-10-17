// Dabble 헤더를 넣는 파일은 이것 하나뿐이어야 합니다 (모듈 객체가 헤더에서 만들어짐).
#define CUSTOM_SETTINGS
#define INCLUDE_GAMEPAD_MODULE
#include <DabbleESP32.h>
#include "ble_control.h"
#include "rcar_config.h"

void bleBegin(const char* name) { Dabble.begin(name); }

PadInput blePoll() {
  static bool prevStart = false;
  Dabble.processInput();
  PadInput in{};
  in.connected = Dabble.isAppConnected();
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
  in.startEdge = s && !prevStart;
  prevStart = s;
  if (!in.connected) { in.throttle = 0; in.steer = 0; in.startEdge = false; }
  return in;
}

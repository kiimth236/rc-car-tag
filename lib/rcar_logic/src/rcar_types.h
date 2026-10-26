#pragma once
#include <stdint.h>

namespace rcar {

enum class Role : uint8_t { Chaser = 0, Runner = 1 };
enum class Phase : uint8_t { Idle = 0, Countdown = 1, Playing = 2, Over = 3 };
enum class BatteryState : uint8_t { NoBattery, Ok, Warn, Cutoff };
enum class LedPattern : uint8_t { IdleBlink, Countdown, RoleSolid, Caught, ChaserLocked, ChaserWin };
enum class UiMode : uint8_t { Menu = 0, Lobby = 1, Solo = 2, Rename = 3 };  // 폰 화면 모드
enum class Winner : uint8_t { None = 0, Chaser = 1, Runners = 2 };
enum class DriveLabel : uint8_t { Stop = 0, Forward, Back, Left, Right, SpinLeft, SpinRight, Blocked };  // 화면에 보일 주행 글자

struct Rgb {
  uint8_t r, g, b;
};

}  // namespace rcar

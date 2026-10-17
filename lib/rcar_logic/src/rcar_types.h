#pragma once
#include <stdint.h>

namespace rcar {

enum class Role : uint8_t { Chaser = 0, Runner = 1 };
enum class Phase : uint8_t { Idle = 0, Countdown = 1, Playing = 2, Over = 3 };
enum class BatteryState : uint8_t { NoBattery, Ok, Warn, Cutoff };
enum class LedPattern : uint8_t { IdleBlink, Countdown, RoleSolid, Caught, ChaserLocked, ChaserWin };

struct Rgb {
  uint8_t r, g, b;
};

}  // namespace rcar

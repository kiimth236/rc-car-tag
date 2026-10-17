#include "led_patterns.h"

namespace rcar {

static const Rgb kOff{0, 0, 0};

Rgb roleColor(Role role, uint8_t carId, uint8_t level) {
  if (role == Role::Chaser) return {level, level, level};
  switch (carId) {
    case 1: return {0, level, 0};
    case 2: return {0, 0, level};
    case 3: return {level, level, 0};
    default: return {0, level, level};
  }
}

Rgb patternColor(LedPattern p, Role role, uint8_t carId, uint32_t countdownMsLeft, BatteryState bs, uint32_t now,
                 uint8_t level) {
  const Rgb red{level, 0, 0};
  if (bs == BatteryState::Cutoff) return (now % 500) < 250 ? red : kOff;
  if (bs == BatteryState::Warn && (now % 2000) < 200) return {level, level, 0};
  Rgb role_c = roleColor(role, carId, level);
  switch (p) {
    case LedPattern::IdleBlink: return (now % 1000) < 500 ? role_c : kOff;
    case LedPattern::Countdown: return (countdownMsLeft % 1000) > 700 ? role_c : kOff;
    case LedPattern::RoleSolid: return role_c;
    case LedPattern::Caught:
    case LedPattern::ChaserLocked: return red;
    case LedPattern::ChaserWin: {
      static const Rgb wheel[6] = {{1, 0, 0}, {1, 1, 0}, {0, 1, 0}, {0, 1, 1}, {0, 0, 1}, {1, 0, 1}};
      const Rgb& w = wheel[(now / 150) % 6];
      return {(uint8_t)(w.r * level), (uint8_t)(w.g * level), (uint8_t)(w.b * level)};
    }
  }
  return kOff;
}

}  // namespace rcar

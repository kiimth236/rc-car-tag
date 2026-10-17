#pragma once
#include <stdint.h>
#include "rcar_types.h"

namespace rcar {

// 추격 흰색 · 도주 차 번호 1 초록 · 2 파랑 · 3 노랑 · 4 하늘색
Rgb roleColor(Role role, uint8_t carId, uint8_t level);

// 상태 → 지금 켤 색. 배터리 정지는 빨강 빠른 깜빡임, 경고는 2초마다 노랑 0.2초
Rgb patternColor(LedPattern p, Role role, uint8_t carId, uint32_t countdownMsLeft, BatteryState bs, uint32_t now,
                 uint8_t level);

}  // namespace rcar

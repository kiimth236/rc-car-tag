#pragma once
#include <stddef.h>
#include <stdint.h>
#include "rcar_types.h"
#include "names.h"

namespace rcar {

// 폰 → 차 (WebSocket 글자 한 줄)
//   "J <x> <y>"  조이스틱 −100~100 (x 오른쪽+, y 앞+)   "S" SELECT   "G" START
//   "M <menu|lobby|solo|rename>"   "T +" / "T -"   "P <1~4>" 참가 토글   "N <이름>" (빈 이름 = "N")
struct WebCmd {
  enum class Kind : uint8_t { None, Joy, Select, Start, Mode, Time, Toggle, Name } kind;
  int8_t x, y;
  UiMode mode;
  int8_t dir;
  uint8_t id;
  char name[kNameBytes + 1];
};

// text는 NUL로 끝나지 않을 수 있음 (len까지만 읽음). 틀리면 false.
// 범위 밖 x·y는 −100~100으로 자르고, 이름은 validName이 아니면 false.
bool parseWebCmd(const char* text, size_t len, WebCmd& out);

// 차 → 폰 상태 (JSON 한 줄에 쓸 값 모음)
struct UiSnapshot {
  uint8_t id;
  Role role;
  const char* name;
  UiMode mode;
  Phase phase;
  bool spectating, caughtMe, lock;
  uint8_t participants, caughtMask, eligible;
  uint32_t msLeft;
  uint16_t gameSec;
  Winner winner;
  DriveLabel label;
  uint8_t battDeciV;
  BatteryState bs;
  int16_t distCm;  // −1 = 없음/안 잼
  const char* names[5];  // [1..4], 비면 ""
};

// 쓴 길이를 돌려줌. buf가 모자라면 0. validName이 아닌 이름은 ""로 씀.
size_t buildStateJson(const UiSnapshot& s, char* buf, size_t cap);

}  // namespace rcar

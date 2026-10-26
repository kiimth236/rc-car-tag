#pragma once
#include <stddef.h>
#include <stdint.h>
#include "names.h"
#include "rcar_types.h"

namespace rcar {

enum class MsgType : uint8_t { Status = 1, Start = 2, Caught = 3, Ack = 4 };

constexpr uint16_t kTeamMagic = 0x52C6;  // 다른 팀 ESP-NOW 메시지를 걸러 내는 표시

struct __attribute__((packed)) Message {
  uint16_t magic;
  uint8_t type;       // MsgType
  uint8_t from;       // 보낸 차 번호 1~4
  uint16_t round;     // 판 번호
  uint8_t role;       // Role
  uint8_t phase;      // Phase
  uint8_t caught;     // 0/1
  uint8_t target;     // ACK 대상 차 번호
  uint8_t battDeciV;  // 배터리 0.1V 단위
  uint8_t mode;          // UiMode (보낸 차의 화면)
  uint8_t phoneOk;       // 0/1 폰 연결(유예 포함)
  uint8_t participants;  // 비트 i = 차 i 참가 (추격차가 정함)
  uint8_t caughtMask;    // 비트 i = 차 i 잡힘 (추격차 기준)
  uint8_t winner;        // Winner
  uint16_t gameSec;      // 이번 판(또는 고른) 게임 시간 초
  uint32_t msLeft;       // 카운트다운 중이면 카운트다운 남은 ms, 게임 중이면 게임 남은 ms, 그 밖 0
  char name[kNameBytes]; // UTF-8, 남는 칸 0
};
static_assert(sizeof(Message) == 30, "Message must be 30 bytes");

Message makeMessage(MsgType t, uint8_t from, uint16_t round);
size_t encode(const Message& m, uint8_t* buf, size_t cap);
bool decode(const uint8_t* data, size_t len, uint16_t expectedMagic, Message& out);

}  // namespace rcar

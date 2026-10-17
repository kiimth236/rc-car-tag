#pragma once
#include <stdint.h>
#include "protocol.h"
#include "rcar_types.h"

namespace rcar {

struct GameConfig {
  uint32_t countdownMs;     // 3000
  uint32_t chaserLockMs;    // 잡은 뒤 추격차 정지 1000
  uint32_t statusPeriodMs;  // 200
  uint32_t resendMs;        // START·CAUGHT 반복 간격 50
  uint32_t startRepeatMs;   // START 반복 시간 300
  uint32_t runnerAliveMs;   // 도주차 상태가 이 안에 왔으면 "켜져 있음" 1000
  uint32_t chaserLostMs;    // 도주차가 추격차를 못 보면 대기로 2000
  uint32_t emptyGameMs;     // 게임 중 도주차가 하나도 없으면 대기로 2000
};

constexpr GameConfig kDefaultGameConfig{3000, 1000, 200, 50, 300, 1000, 2000, 2000};

class Game {
 public:
  Game(Role role, uint8_t carId, const GameConfig& cfg = kDefaultGameConfig);

  void onStartPressed(uint32_t now);
  void onHallTriggered(uint32_t now);
  void onMessage(const Message& m, uint32_t now);
  void tick(uint32_t now);

  bool popOutgoing(Message& out);
  bool takeBrake();  // 급정지 신호 (한 번 읽으면 지워짐)
  void setBatteryDeciV(uint8_t v) { battDeciV_ = v; }

  Phase phase() const { return phase_; }
  bool caught() const { return caught_; }
  uint16_t round() const { return round_; }
  bool motorsEnabled(uint32_t now) const;
  bool hallArmed() const;
  LedPattern led(uint32_t now) const;
  uint32_t countdownMsLeft(uint32_t now) const;

 private:
  struct Peer {
    bool seen;
    uint32_t lastMs;
    uint16_t round;
    Phase phase;
    Role role;
  };
  static bool reached(uint32_t now, uint32_t t) { return (int32_t)(now - t) >= 0; }
  Message make(MsgType t, uint32_t now) const;
  void push(const Message& m);
  void enterCountdown(uint16_t round, uint32_t endAt, uint32_t now);
  void chaserCheckEnd(uint32_t now);

  Role role_;
  uint8_t id_;
  GameConfig cfg_;
  Phase phase_ = Phase::Idle;
  uint16_t round_ = 0;
  bool caught_ = false;
  bool brake_ = false;
  uint8_t battDeciV_ = 0;
  uint32_t countdownEnd_ = 0, lockUntil_ = 0, nextStatus_ = 0;
  uint32_t startRepeatUntil_ = 0, nextStartRepeat_ = 0, playingSince_ = 0;
  bool awaitingAck_ = false;
  uint32_t nextCaughtResend_ = 0;
  uint32_t lastChaserSeen_ = 0;
  Peer peers_[5] = {};      // 차 번호 1~4
  bool caughtSet_[5] = {};  // 추격차: 이번 판에 잡힌 도주차
  Message queue_[8];
  uint8_t qHead_ = 0, qCount_ = 0;
};

}  // namespace rcar

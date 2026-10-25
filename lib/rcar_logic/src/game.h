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
  uint32_t runnerAliveMs;   // 이 안에 상태가 왔으면 "켜져 있음" 1000
  uint32_t chaserLostMs;    // 도주차가 추격차를 못 보면 대기로 2000
  uint32_t offlineCatchMs;  // 참가 도주차가 이만큼 조용하면 잡힘 10000
  uint16_t gameSecDefault;  // 게임 시간 기본 180초
};

constexpr GameConfig kDefaultGameConfig{3000, 1000, 200, 50, 300, 1000, 2000, 10000, 180};

// 게임 시간 단계: dir +1/−1 = 30초 더하기/빼기, 0 = 맞추기만. 결과는 항상 60~300, 30의 배수 (30 배수 아니면 아래로 내림)
uint16_t stepGameSeconds(uint16_t cur, int dir);

class Game {
 public:
  Game(Role role, uint8_t carId, const GameConfig& cfg = kDefaultGameConfig);

  bool onStartPressed(uint32_t now);  // 시작했으면 true
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

  // 화면 모드 · 참가 고르기 · 시간 · 이름
  UiMode mode() const { return mode_; }
  bool setMode(UiMode m);       // 참가 중(카운트다운·게임 중) Lobby 떠나기는 거부(false)
  void onSelect(uint32_t now);  // SELECT 버튼
  void setPhoneOk(bool ok) { phoneOk_ = ok; }
  void setName(const char* utf8);  // validName 아니면 무시
  const char* name() const { return name_; }
  const char* peerName(uint8_t id) const;  // 1~4, 모르면 ""
  void setGameSeconds(uint16_t sec);       // stepGameSeconds(sec, 0) 으로 맞춰 저장
  uint16_t gameSeconds() const { return gameSec_; }
  void toggleParticipant(uint8_t id, uint32_t now);  // 추격차·Lobby·Idle/Over 에서만, eligible 인 차만
  uint8_t eligibleMask(uint32_t now) const;          // 추격차: 고를 수 있는 도주차
  uint8_t participantsMask(uint32_t now) const;      // 대기: eligible & ~제외, 판 중: 이번 판 참가
  uint8_t caughtMask() const { return caughtSet_; }
  Winner winner() const { return winner_; }
  bool spectating() const { return spectating_; }  // 이번 판 관전
  uint32_t msLeft(uint32_t now) const;  // 카운트다운이면 카운트다운 남은 ms, 게임 중이면 게임 남은 ms, 그 밖 0
  bool roundRunningNearby(uint32_t now) const;  // 내가 또는 최근 본 추격차가 카운트다운·게임 중
  bool takeRejoined();  // 도주차가 이미 진행 중인 판(게임 중)에 상태 메시지로 들어왔으면 한 번 true

 private:
  struct Peer {
    bool seen;
    uint32_t lastMs;  // 아무 메시지나 마지막으로 들은 시각
    uint16_t round;
    Phase phase;
    Role role;
    UiMode mode;
    bool phoneOk;
    bool caught;
    char name[kNameBytes + 1];
  };
  static bool reached(uint32_t now, uint32_t t) { return (int32_t)(now - t) >= 0; }
  static uint8_t carBit(uint8_t id) { return (uint8_t)(1u << id); }
  bool running() const { return phase_ == Phase::Countdown || phase_ == Phase::Playing; }
  bool inRound() const { return (participants_ & carBit(id_)) && !spectating_; }  // 이번 판 참가 (도주차)
  Message make(MsgType t, uint32_t now) const;
  void push(const Message& m);
  void beginRound(uint16_t round, Phase p, uint32_t msLeft, uint32_t now);
  void toIdle();
  void notePeer(const Message& m, uint32_t now);
  void runnerFollowChaser(const Message& m, uint32_t now);
  void chaserTick(uint32_t now);

  Role role_;
  uint8_t id_;
  GameConfig cfg_;
  UiMode mode_ = UiMode::Menu;
  Phase phase_ = Phase::Idle;
  uint16_t round_ = 0;
  bool caught_ = false;
  bool brake_ = false;
  bool phoneOk_ = false;
  bool spectating_ = false;
  bool rejoined_ = false;
  Winner winner_ = Winner::None;
  uint8_t battDeciV_ = 0;
  uint16_t gameSec_;
  char name_[kNameBytes + 1] = {};
  uint32_t countdownEnd_ = 0, gameEnd_ = 0, lockUntil_ = 0, nextStatus_ = 0;
  uint32_t startRepeatUntil_ = 0, nextStartRepeat_ = 0;
  bool awaitingAck_ = false;
  uint32_t nextCaughtResend_ = 0;
  uint32_t lastChaserSeen_ = 0;
  Peer peers_[5] = {};        // 차 번호 1~4
  uint8_t excluded_ = 0;      // 추격차: 대기 화면에서 뺀 차 (비트 i = 차 i)
  uint8_t participants_ = 0;  // 이번 판 참가 (추격차가 정함, 도주차는 받은 값)
  uint8_t caughtSet_ = 0;     // 이번 판 잡힘 (추격차가 정함, 도주차는 받은 값)
  Message queue_[8];
  uint8_t qHead_ = 0, qCount_ = 0;
};

}  // namespace rcar

#include "game.h"
#include <string.h>

namespace rcar {

uint16_t stepGameSeconds(uint16_t cur, int dir) {
  int v = cur / 30 * 30;  // 30의 배수로 내림
  if (v < 60) v = 60;
  if (v > 300) v = 300;
  if (dir > 0) v += 30;
  if (dir < 0) v -= 30;
  if (v < 60) v = 60;
  if (v > 300) v = 300;
  return (uint16_t)v;
}

Game::Game(Role role, uint8_t carId, const GameConfig& cfg)
    : role_(role), id_(carId), cfg_(cfg), gameSec_(stepGameSeconds(cfg.gameSecDefault, 0)) {}

Message Game::make(MsgType t, uint32_t now) const {
  Message m = makeMessage(t, id_, round_);
  m.role = (uint8_t)role_;
  m.phase = (uint8_t)phase_;
  m.caught = caught_ ? 1 : 0;
  m.battDeciV = battDeciV_;
  m.mode = (uint8_t)mode_;
  m.phoneOk = phoneOk_ ? 1 : 0;
  m.participants = participantsMask(now);
  m.caughtMask = caughtSet_;
  m.winner = (uint8_t)winner_;
  m.gameSec = gameSec_;
  m.msLeft = msLeft(now);
  memcpy(m.name, name_, kNameBytes);
  return m;
}

void Game::push(const Message& m) {
  if (qCount_ == 8) { qHead_ = (uint8_t)((qHead_ + 1) % 8); qCount_--; }  // 가득 차면 가장 오래된 것 버림
  queue_[(qHead_ + qCount_) % 8] = m;
  qCount_++;
}

bool Game::popOutgoing(Message& out) {
  if (qCount_ == 0) return false;
  out = queue_[qHead_];
  qHead_ = (uint8_t)((qHead_ + 1) % 8);
  qCount_--;
  return true;
}

bool Game::takeBrake() {
  bool b = brake_;
  brake_ = false;
  return b;
}

bool Game::takeRejoined() {
  bool r = rejoined_;
  rejoined_ = false;
  return r;
}

// 새 판에 들어감. p = Countdown 이면 msLeft 는 카운트다운 남은 ms, Playing 이면 게임 남은 ms
void Game::beginRound(uint16_t round, Phase p, uint32_t msLeft, uint32_t now) {
  round_ = round;
  phase_ = p;
  if (p == Phase::Countdown) countdownEnd_ = now + msLeft;
  else gameEnd_ = now + msLeft;
  caught_ = false;
  awaitingAck_ = false;
  winner_ = Winner::None;
  overMsLeft_ = 0;
  lockUntil_ = now;
  lastChaserSeen_ = now;
}

// 대기로: 지난 판 상태(잡힘·관전·우승)를 버림
void Game::toIdle() {
  phase_ = Phase::Idle;
  caught_ = false;
  awaitingAck_ = false;
  spectating_ = false;
  winner_ = Winner::None;
}

bool Game::setMode(UiMode m) {
  if ((uint8_t)m > (uint8_t)UiMode::Rename) return false;
  if (m == mode_) return true;
  if (mode_ == UiMode::Lobby && running()) return false;  // 참가·관전 중(관전 차도 Lobby): 판이 끝날 때까지 못 떠남
  mode_ = m;
  if (m != UiMode::Lobby) toIdle();  // 게임 화면을 떠나면 항상 대기
  return true;
}

void Game::onSelect(uint32_t now) {
  (void)now;
  switch (mode_) {
    case UiMode::Menu: return;
    case UiMode::Solo:
    case UiMode::Rename: setMode(UiMode::Menu); return;
    case UiMode::Lobby:
      if (phase_ == Phase::Idle) setMode(UiMode::Menu);
      else if (phase_ == Phase::Over) toIdle();  // 결과 닫기 → 게임 대기
      return;                                    // 카운트다운·게임 중(참가·관전) 은 무시
  }
}

void Game::setName(const char* utf8) {
  if (!validName(utf8)) return;
  memset(name_, 0, sizeof name_);
  strncpy(name_, utf8, kNameBytes);
}

const char* Game::peerName(uint8_t id) const {
  if (id < 1 || id > 4) return "";
  if (id == id_) return name_;
  return peers_[id].name;
}

void Game::setGameSeconds(uint16_t sec) {
  if (role_ == Role::Chaser && running()) return;  // 판 도중에는 이번 판 시간을 바꾸지 않음
  gameSec_ = stepGameSeconds(sec, 0);
}

uint8_t Game::eligibleMask(uint32_t now) const {
  if (role_ != Role::Chaser) return 0;
  uint8_t mask = 0;
  for (uint8_t i = 1; i <= 4; i++) {
    if (i == id_) continue;
    const Peer& p = peers_[i];
    if (p.seen && p.role == Role::Runner && p.mode == UiMode::Lobby && p.phoneOk &&
        !reached(now, p.lastMs + cfg_.runnerAliveMs))
      mask |= carBit(i);
  }
  return mask;
}

uint8_t Game::participantsMask(uint32_t now) const {
  if (role_ == Role::Chaser) {
    if (phase_ != Phase::Idle) return participants_;
    if (mode_ != UiMode::Lobby) return 0;  // 메뉴·혼자 놀기·이름 바꾸기: 판을 열지 않았으니 참가 없음
    return (uint8_t)(eligibleMask(now) & ~excluded_);
  }
  // 도주차: 추격차가 알려 준 값. 대기 중 추격차가 안 보이면 없음
  if (phase_ == Phase::Idle && reached(now, lastChaserSeen_ + cfg_.chaserLostMs)) return 0;
  return participants_;
}

void Game::toggleParticipant(uint8_t id, uint32_t now) {
  if (role_ != Role::Chaser || mode_ != UiMode::Lobby || running()) return;
  if (id < 1 || id > 4 || !(eligibleMask(now) & carBit(id))) return;
  excluded_ ^= carBit(id);
}

bool Game::onStartPressed(uint32_t now) {
  if (role_ != Role::Chaser || mode_ != UiMode::Lobby) return false;
  uint8_t p;
  if (phase_ == Phase::Idle) {
    p = participantsMask(now);
  } else if (phase_ == Phase::Over) {
    excluded_ = 0;  // 다시 하기: 그 순간 고를 수 있는 차 전부
    p = eligibleMask(now);
  } else {
    return false;  // 카운트다운·게임 중 START 무시
  }
  if (p == 0) return false;
  participants_ = p;
  caughtSet_ = 0;
  beginRound((uint16_t)(round_ + 1), Phase::Countdown, cfg_.countdownMs, now);
  startRepeatUntil_ = now + cfg_.startRepeatMs;
  nextStartRepeat_ = now + cfg_.resendMs;
  push(make(MsgType::Start, now));
  return true;
}

void Game::onHallTriggered(uint32_t now) {
  if (!hallArmed()) return;
  caught_ = true;
  brake_ = true;
  awaitingAck_ = true;
  nextCaughtResend_ = now + cfg_.resendMs;
  push(make(MsgType::Caught, now));
}

void Game::notePeer(const Message& m, uint32_t now) {
  MsgType t = (MsgType)m.type;
  Peer& p = peers_[m.from];
  p.seen = true;
  p.lastMs = now;
  p.role = (Role)m.role;
  p.round = m.round;
  p.phase = t == MsgType::Start ? Phase::Countdown : (Phase)m.phase;
  if (t != MsgType::Status) return;
  p.mode = (UiMode)m.mode;
  p.phoneOk = m.phoneOk != 0;
  p.caught = m.caught != 0;
  char n[kNameBytes + 1];
  memcpy(n, m.name, kNameBytes);
  n[kNameBytes] = 0;
  if (validName(n)) memcpy(p.name, n, sizeof n);  // 이상한 이름은 저장 안 함
}

// 도주차: 추격차의 START·STATUS 를 따름 (판 정보·시간·결과는 추격차가 기준)
void Game::runnerFollowChaser(const Message& m, uint32_t now) {
  lastChaserSeen_ = now;
  gameSec_ = stepGameSeconds(m.gameSec, 0);
  participants_ = m.participants;
  caughtSet_ = m.caughtMask;
  bool isStatus = (MsgType)m.type == MsgType::Status;
  Phase cp = isStatus ? (Phase)m.phase : Phase::Countdown;
  bool mine = (m.participants & carBit(id_)) != 0;

  if (cp == Phase::Idle) {
    if (phase_ != Phase::Idle || spectating_) toIdle();
    return;
  }
  if (cp == Phase::Over) {
    if (m.round != round_) {
      if (phase_ != Phase::Idle) toIdle();  // 다른 판의 끝: 지난 판 상태를 버림
    } else if (running()) {
      phase_ = Phase::Over;  // 결과를 닫은 뒤(Idle)에는 다시 Over 로 가지 않음
      winner_ = (Winner)m.winner;
      overMsLeft_ = m.msLeft;  // 끝난 순간 남은 시간 (추격차 기준)
      spectating_ = false;     // 관전은 판이 끝날 때까지만 → 결과 화면에서 SELECT 로 대기
    }
    return;
  }

  // 추격차가 카운트다운·게임 중
  if (m.round != round_ || !running()) {
    if (mine) {
      mode_ = UiMode::Lobby;  // 참가 차는 어느 화면에 있었든 판으로
      spectating_ = false;
      beginRound(m.round, cp, m.msLeft, now);
      caught_ = (caughtSet_ & carBit(id_)) != 0;
      if (cp == Phase::Playing) rejoined_ = true;
    } else if (mode_ != UiMode::Solo) {
      mode_ = UiMode::Lobby;  // 혼자 놀기가 아니면 관전
      beginRound(m.round, cp, m.msLeft, now);
      spectating_ = true;
    }
    return;
  }

  // 같은 판 진행 중
  if (cp == Phase::Playing && isStatus) {
    if (phase_ == Phase::Countdown) phase_ = Phase::Playing;
    gameEnd_ = now + m.msLeft;  // 남은 시간은 추격차 기준으로 다시 맞춤
  }
  if (inRound() && (caughtSet_ & carBit(id_))) {
    awaitingAck_ = false;  // 추격차가 이미 잡힘으로 앎 → CAUGHT 재전송 중지
    if (phase_ == Phase::Playing && !caught_) {
      caught_ = true;  // 꺼진 것으로 쳐져 잡힘 처리된 경우 등
      brake_ = true;
    }
  }
}

void Game::onMessage(const Message& m, uint32_t now) {
  if (m.from == id_ || m.from < 1 || m.from > 4) return;
  notePeer(m, now);
  MsgType t = (MsgType)m.type;
  Role fromRole = (Role)m.role;

  if (role_ == Role::Runner) {
    if ((t == MsgType::Status || t == MsgType::Start) && fromRole == Role::Chaser) {
      runnerFollowChaser(m, now);
    } else if (t == MsgType::Ack && m.target == id_ && m.round == round_) {
      awaitingAck_ = false;
    }
    return;
  }

  // 추격차: 이번 판 CAUGHT 는 ACK, 참가 차의 첫 CAUGHT 만 잡힘으로 셈
  if (t == MsgType::Caught && m.round == round_ && (phase_ == Phase::Playing || phase_ == Phase::Over)) {
    Message a = make(MsgType::Ack, now);
    a.target = m.from;
    push(a);
    uint8_t b = carBit(m.from);
    if (phase_ == Phase::Playing && (participants_ & b) && !(caughtSet_ & b)) {
      caughtSet_ |= b;
      brake_ = true;
      lockUntil_ = now + cfg_.chaserLockMs;
    }
  }
}

void Game::chaserTick(uint32_t now) {
  if (phase_ == Phase::Countdown && !reached(now, startRepeatUntil_) && reached(now, nextStartRepeat_)) {
    push(make(MsgType::Start, now));
    nextStartRepeat_ = now + cfg_.resendMs;
  }
  if (phase_ != Phase::Playing) return;
  // 꺼진(조용한) 참가 도주차는 잡힌 것으로 침 (급정지·잠금 없음)
  for (uint8_t i = 1; i <= 4; i++) {
    uint8_t b = carBit(i);
    if ((participants_ & b) && !(caughtSet_ & b) && reached(now, peers_[i].lastMs + cfg_.offlineCatchMs))
      caughtSet_ |= b;
  }
  if ((participants_ & ~caughtSet_) == 0) {
    overMsLeft_ = msLeft(now);  // 모두 잡음: 끝난 순간 남은 시간을 결과 화면에 그대로 둠
    phase_ = Phase::Over;
    winner_ = Winner::Chaser;
  } else if (reached(now, gameEnd_)) {
    overMsLeft_ = 0;
    phase_ = Phase::Over;
    winner_ = Winner::Runners;
  }
}

void Game::tick(uint32_t now) {
  if (phase_ == Phase::Countdown && reached(now, countdownEnd_)) {
    phase_ = Phase::Playing;
    gameEnd_ = countdownEnd_ + (uint32_t)gameSec_ * 1000;
  }
  if (role_ == Role::Chaser) {
    chaserTick(now);
  } else {
    if (awaitingAck_ && reached(now, nextCaughtResend_)) {
      push(make(MsgType::Caught, now));
      nextCaughtResend_ = now + cfg_.resendMs;
    }
    if (phase_ != Phase::Idle && reached(now, lastChaserSeen_ + cfg_.chaserLostMs)) toIdle();
  }
  if (reached(now, nextStatus_)) {
    push(make(MsgType::Status, now));
    nextStatus_ = now + cfg_.statusPeriodMs;
  }
}

bool Game::motorsEnabled(uint32_t now) const {
  if (mode_ == UiMode::Solo) return true;
  if (mode_ != UiMode::Lobby) return false;  // 메뉴·이름 바꾸기
  switch (phase_) {
    case Phase::Idle: return true;
    case Phase::Countdown: return false;
    case Phase::Playing: return role_ == Role::Chaser ? reached(now, lockUntil_) : inRound() && !caught_;
    default: return false;
  }
}

bool Game::hallArmed() const {
  return role_ == Role::Runner && mode_ == UiMode::Lobby && phase_ == Phase::Playing && inRound() && !caught_;
}

LedPattern Game::led(uint32_t now) const {
  if (mode_ == UiMode::Solo) return LedPattern::RoleSolid;
  if (mode_ != UiMode::Lobby) return LedPattern::IdleBlink;
  if (phase_ == Phase::Over) {
    bool chaserWon = winner_ != Winner::Runners;
    if (role_ == Role::Chaser) return chaserWon ? LedPattern::ChaserWin : LedPattern::Caught;
    return chaserWon ? LedPattern::Caught : LedPattern::RoleSolid;
  }
  if (spectating_ || phase_ == Phase::Idle) return LedPattern::IdleBlink;
  if (phase_ == Phase::Countdown) return LedPattern::Countdown;
  if (role_ == Role::Runner && caught_) return LedPattern::Caught;
  if (role_ == Role::Chaser && !reached(now, lockUntil_)) return LedPattern::ChaserLocked;
  return LedPattern::RoleSolid;
}

uint32_t Game::countdownMsLeft(uint32_t now) const {
  if (phase_ != Phase::Countdown || reached(now, countdownEnd_)) return 0;
  return countdownEnd_ - now;
}

uint32_t Game::msLeft(uint32_t now) const {
  if (phase_ == Phase::Countdown) return countdownMsLeft(now);
  if (phase_ == Phase::Over) return overMsLeft_;  // 결과 화면: 끝난 순간의 남은 시간 (시간 끝 = 0)
  if (phase_ != Phase::Playing || reached(now, gameEnd_)) return 0;  // 도주차는 0에서 멈추고 추격차의 끝을 기다림
  return gameEnd_ - now;
}

bool Game::roundRunningNearby(uint32_t now) const {
  if (running()) return true;
  for (uint8_t i = 1; i <= 4; i++) {
    const Peer& p = peers_[i];
    if (i != id_ && p.seen && p.role == Role::Chaser && (p.phase == Phase::Countdown || p.phase == Phase::Playing) &&
        !reached(now, p.lastMs + cfg_.runnerAliveMs))
      return true;
  }
  return false;
}

}  // namespace rcar

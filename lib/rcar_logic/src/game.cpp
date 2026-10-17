#include "game.h"

namespace rcar {

Game::Game(Role role, uint8_t carId, const GameConfig& cfg) : role_(role), id_(carId), cfg_(cfg) {}

Message Game::make(MsgType t, uint32_t now) const {
  Message m = makeMessage(t, id_, round_);
  m.role = (uint8_t)role_;
  m.phase = (uint8_t)phase_;
  m.caught = caught_ ? 1 : 0;
  m.battDeciV = battDeciV_;
  m.ms = (uint16_t)countdownMsLeft(now);
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

void Game::enterCountdown(uint16_t round, uint32_t endAt, uint32_t now) {
  round_ = round;
  phase_ = Phase::Countdown;
  countdownEnd_ = endAt;
  caught_ = false;
  awaitingAck_ = false;
  lockUntil_ = now;
  lastChaserSeen_ = now;
  for (bool& c : caughtSet_) c = false;
}

void Game::onStartPressed(uint32_t now) {
  if (role_ != Role::Chaser) return;
  if (phase_ != Phase::Idle && phase_ != Phase::Over) return;  // 게임 중 START 무시
  enterCountdown((uint16_t)(round_ + 1), now + cfg_.countdownMs, now);
  startRepeatUntil_ = now + cfg_.startRepeatMs;
  nextStartRepeat_ = now + cfg_.resendMs;
  push(make(MsgType::Start, now));
}

void Game::onHallTriggered(uint32_t now) {
  if (role_ != Role::Runner || phase_ != Phase::Playing || caught_) return;
  caught_ = true;
  brake_ = true;
  awaitingAck_ = true;
  nextCaughtResend_ = now + cfg_.resendMs;
  push(make(MsgType::Caught, now));
}

void Game::onMessage(const Message& m, uint32_t now) {
  if (m.from == id_ || m.from < 1 || m.from > 4) return;
  MsgType t = (MsgType)m.type;
  Role fromRole = (Role)m.role;

  if (t == MsgType::Status) {
    Peer& p = peers_[m.from];
    p.seen = true;
    p.lastMs = now;
    p.round = m.round;
    p.phase = (Phase)m.phase;
    p.role = fromRole;
    if (role_ == Role::Runner && fromRole == Role::Chaser) {
      lastChaserSeen_ = now;
      if ((Phase)m.phase == Phase::Over && m.round == round_ && phase_ == Phase::Playing) phase_ = Phase::Over;
    }
    return;
  }

  if (role_ == Role::Runner) {
    if (t == MsgType::Start && fromRole == Role::Chaser) {
      lastChaserSeen_ = now;
      bool sameRoundRunning = m.round == round_ && (phase_ == Phase::Countdown || phase_ == Phase::Playing);
      if (!sameRoundRunning) enterCountdown(m.round, now + m.ms, now);
    } else if (t == MsgType::Ack && m.target == id_ && m.round == round_) {
      awaitingAck_ = false;
    }
    return;
  }

  // 추격차
  if (t == MsgType::Caught && m.round == round_ && (phase_ == Phase::Playing || phase_ == Phase::Over)) {
    Message a = make(MsgType::Ack, now);
    a.target = m.from;
    push(a);
    if (phase_ == Phase::Playing && !caughtSet_[m.from]) {
      caughtSet_[m.from] = true;
      brake_ = true;
      lockUntil_ = now + cfg_.chaserLockMs;
    }
  }
}

void Game::chaserCheckEnd(uint32_t now) {
  int participants = 0, caughtCount = 0;
  for (uint8_t i = 1; i <= 4; i++) {
    if (i == id_) continue;
    if (caughtSet_[i]) { participants++; caughtCount++; continue; }
    const Peer& p = peers_[i];
    bool active = p.seen && p.role == Role::Runner && !reached(now, p.lastMs + cfg_.runnerAliveMs) &&
                  p.round == round_ && (p.phase == Phase::Countdown || p.phase == Phase::Playing);
    if (active) participants++;
  }
  if (participants > 0 && caughtCount == participants) {
    phase_ = Phase::Over;
  } else if (participants == 0 && reached(now, playingSince_ + cfg_.emptyGameMs)) {
    phase_ = Phase::Idle;  // 도주차가 하나도 없으면 대기로
  }
}

void Game::tick(uint32_t now) {
  if (phase_ == Phase::Countdown && reached(now, countdownEnd_)) {
    phase_ = Phase::Playing;
    playingSince_ = now;
  }
  if (role_ == Role::Chaser) {
    if (phase_ == Phase::Countdown && !reached(now, startRepeatUntil_) && reached(now, nextStartRepeat_)) {
      push(make(MsgType::Start, now));
      nextStartRepeat_ = now + cfg_.resendMs;
    }
    if (phase_ == Phase::Playing) chaserCheckEnd(now);
  } else {
    if (awaitingAck_ && reached(now, nextCaughtResend_)) {
      push(make(MsgType::Caught, now));
      nextCaughtResend_ = now + cfg_.resendMs;
    }
    if ((phase_ == Phase::Countdown || phase_ == Phase::Playing) && reached(now, lastChaserSeen_ + cfg_.chaserLostMs)) {
      phase_ = Phase::Idle;
      caught_ = false;
      awaitingAck_ = false;
    }
  }
  if (reached(now, nextStatus_)) {
    push(make(MsgType::Status, now));
    nextStatus_ = now + cfg_.statusPeriodMs;
  }
}

bool Game::motorsEnabled(uint32_t now) const {
  switch (phase_) {
    case Phase::Idle: return true;
    case Phase::Countdown: return false;
    case Phase::Playing: return role_ == Role::Chaser ? reached(now, lockUntil_) : !caught_;
    default: return false;
  }
}

bool Game::hallArmed() const { return role_ == Role::Runner && phase_ == Phase::Playing && !caught_; }

LedPattern Game::led(uint32_t now) const {
  switch (phase_) {
    case Phase::Idle: return LedPattern::IdleBlink;
    case Phase::Countdown: return LedPattern::Countdown;
    case Phase::Playing:
      if (role_ == Role::Runner && caught_) return LedPattern::Caught;
      if (role_ == Role::Chaser && !reached(now, lockUntil_)) return LedPattern::ChaserLocked;
      return LedPattern::RoleSolid;
    default: return role_ == Role::Chaser ? LedPattern::ChaserWin : LedPattern::Caught;
  }
}

uint32_t Game::countdownMsLeft(uint32_t now) const {
  if (phase_ != Phase::Countdown || reached(now, countdownEnd_)) return 0;
  return countdownEnd_ - now;
}

}  // namespace rcar

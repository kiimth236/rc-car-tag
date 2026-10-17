#include <Arduino.h>
#include <unity.h>
#include "game.h"

using namespace rcar;

void setUp() {}
void tearDown() {}

// 보낼 메시지를 모두 꺼내서 종류별 개수를 셈. 마지막으로 꺼낸 해당 종류는 last 에
static int drain(Game& g, MsgType type, Message* last = nullptr) {
  Message m;
  int n = 0;
  while (g.popOutgoing(m)) {
    if (m.type == (uint8_t)type) { n++; if (last) *last = m; }
  }
  return n;
}
static Message status(uint8_t from, Role role, Phase phase, uint16_t round, bool caught = false) {
  Message m = makeMessage(MsgType::Status, from, round);
  m.role = (uint8_t)role; m.phase = (uint8_t)phase; m.caught = caught ? 1 : 0;
  return m;
}

// 추격차 1번 + 도주차 2번을 서로 메시지를 주고받게 하며 시간을 흘림
struct Pair {
  Game chaser{Role::Chaser, 1};
  Game runner{Role::Runner, 2};
  uint32_t now = 0;
  void step(uint32_t ms) {
    for (uint32_t t = 0; t < ms; t += 10) {
      now += 10;
      chaser.tick(now);
      runner.tick(now);
      Message m;
      while (chaser.popOutgoing(m)) runner.onMessage(m, now);
      while (runner.popOutgoing(m)) chaser.onMessage(m, now);
    }
  }
};

void test_idle_allows_driving() {
  Game g(Role::Runner, 2);
  TEST_ASSERT_EQUAL(Phase::Idle, g.phase());
  TEST_ASSERT_TRUE(g.motorsEnabled(0));
  TEST_ASSERT_FALSE(g.hallArmed());
}
void test_runner_ignores_start_button() {
  Game g(Role::Runner, 2);
  g.onStartPressed(0);
  TEST_ASSERT_EQUAL(Phase::Idle, g.phase());
}
void test_start_countdown_then_playing() {
  Pair p;
  p.step(500);
  p.chaser.onStartPressed(p.now);
  TEST_ASSERT_EQUAL(Phase::Countdown, p.chaser.phase());
  TEST_ASSERT_FALSE(p.chaser.motorsEnabled(p.now));
  p.step(100);
  TEST_ASSERT_EQUAL(Phase::Countdown, p.runner.phase());
  TEST_ASSERT_EQUAL_UINT16(1, p.runner.round());
  p.step(3000);
  TEST_ASSERT_EQUAL(Phase::Playing, p.chaser.phase());
  TEST_ASSERT_EQUAL(Phase::Playing, p.runner.phase());
  TEST_ASSERT_TRUE(p.runner.hallArmed());
  TEST_ASSERT_TRUE(p.runner.motorsEnabled(p.now));
}
void test_start_repeats_for_300ms() {
  Game g(Role::Chaser, 1);
  g.onStartPressed(1000);
  int n = drain(g, MsgType::Start);
  for (uint32_t t = 1010; t <= 1600; t += 10) { g.tick(t); n += drain(g, MsgType::Start); }
  TEST_ASSERT_TRUE(n >= 6 && n <= 7);
}
void test_runner_countdown_uses_remaining_ms() {
  Game r(Role::Runner, 2);
  Message s = makeMessage(MsgType::Start, 1, 1);
  s.role = (uint8_t)Role::Chaser; s.phase = (uint8_t)Phase::Countdown; s.ms = 2800;  // 200ms 늦게 받음
  r.onMessage(s, 5000);
  r.onMessage(status(1, Role::Chaser, Phase::Countdown, 1), 7700);  // 추격차가 계속 보임
  r.tick(7790);
  TEST_ASSERT_EQUAL(Phase::Countdown, r.phase());
  r.tick(7800);
  TEST_ASSERT_EQUAL(Phase::Playing, r.phase());
}
void test_catch_stops_both_and_chaser_resumes_after_1s() {
  Pair p;
  p.step(300);
  p.chaser.onStartPressed(p.now);
  p.step(3200);
  // 도주차 3번이 하나 더 있는 것처럼 상태 알림을 계속 넣어 게임이 바로 끝나지 않게 함
  p.chaser.onMessage(status(3, Role::Runner, Phase::Playing, 1), p.now);
  p.runner.onHallTriggered(p.now);
  TEST_ASSERT_TRUE(p.runner.takeBrake());
  TEST_ASSERT_FALSE(p.runner.motorsEnabled(p.now));
  p.step(20);
  p.chaser.onMessage(status(3, Role::Runner, Phase::Playing, 1), p.now);
  TEST_ASSERT_TRUE(p.chaser.takeBrake());
  TEST_ASSERT_FALSE(p.chaser.motorsEnabled(p.now));
  TEST_ASSERT_EQUAL(LedPattern::ChaserLocked, p.chaser.led(p.now));
  for (int i = 0; i < 10; i++) { p.step(100); p.chaser.onMessage(status(3, Role::Runner, Phase::Playing, 1), p.now); }
  TEST_ASSERT_EQUAL(Phase::Playing, p.chaser.phase());
  TEST_ASSERT_TRUE(p.chaser.motorsEnabled(p.now));
  TEST_ASSERT_FALSE(p.runner.motorsEnabled(p.now));   // 도주차는 계속 정지
  TEST_ASSERT_EQUAL(LedPattern::Caught, p.runner.led(p.now));
}
void test_caught_resent_until_ack() {
  Game r(Role::Runner, 2);
  Message s = makeMessage(MsgType::Start, 1, 1);
  s.role = (uint8_t)Role::Chaser; s.ms = 0;
  r.onMessage(s, 0);
  Message chaserStatus = status(1, Role::Chaser, Phase::Playing, 1);
  r.onMessage(chaserStatus, 0);
  r.tick(0);
  drain(r, MsgType::Caught);
  r.onHallTriggered(10);
  int n = drain(r, MsgType::Caught);
  for (uint32_t t = 20; t <= 210; t += 10) { r.tick(t); r.onMessage(chaserStatus, t); n += drain(r, MsgType::Caught); }
  TEST_ASSERT_TRUE(n >= 4);
  Message ack = makeMessage(MsgType::Ack, 1, 1);
  ack.role = (uint8_t)Role::Chaser; ack.target = 2;
  r.onMessage(ack, 220);
  int after = 0;
  for (uint32_t t = 230; t <= 600; t += 10) { r.tick(t); r.onMessage(chaserStatus, t); after += drain(r, MsgType::Caught); }
  TEST_ASSERT_EQUAL_INT(0, after);
}
void test_duplicate_caught_acked_once_counted() {
  Game c(Role::Chaser, 1);
  c.onStartPressed(0);
  for (uint32_t t = 10; t <= 3100; t += 10) c.tick(t);
  c.onMessage(status(2, Role::Runner, Phase::Playing, 1), 3100);
  c.onMessage(status(3, Role::Runner, Phase::Playing, 1), 3100);
  drain(c, MsgType::Ack);
  Message caught = makeMessage(MsgType::Caught, 2, 1);
  caught.role = (uint8_t)Role::Runner;
  c.onMessage(caught, 3110);
  TEST_ASSERT_TRUE(c.takeBrake());
  c.onMessage(caught, 3160);
  c.onMessage(caught, 3210);
  TEST_ASSERT_FALSE(c.takeBrake());                // 두 번 멈추지 않음
  TEST_ASSERT_EQUAL_INT(3, drain(c, MsgType::Ack)); // ACK는 매번
  c.tick(3220);
  TEST_ASSERT_EQUAL(Phase::Playing, c.phase());     // 3번이 남아 있어 끝나지 않음
}
void test_old_round_caught_ignored() {
  Game c(Role::Chaser, 1);
  c.onStartPressed(0);
  for (uint32_t t = 10; t <= 3100; t += 10) c.tick(t);
  drain(c, MsgType::Ack);
  Message old = makeMessage(MsgType::Caught, 2, 0);  // 지난 판
  old.role = (uint8_t)Role::Runner;
  c.onMessage(old, 3110);
  TEST_ASSERT_FALSE(c.takeBrake());
  TEST_ASSERT_EQUAL_INT(0, drain(c, MsgType::Ack));
}
void test_all_caught_ends_game_and_start_restarts() {
  Pair p;
  p.step(300);
  p.chaser.onStartPressed(p.now);
  p.step(3300);
  p.runner.onHallTriggered(p.now);
  p.step(300);
  TEST_ASSERT_EQUAL(Phase::Over, p.chaser.phase());
  TEST_ASSERT_EQUAL(Phase::Over, p.runner.phase());
  TEST_ASSERT_FALSE(p.chaser.motorsEnabled(p.now));
  TEST_ASSERT_EQUAL(LedPattern::ChaserWin, p.chaser.led(p.now));
  p.chaser.onStartPressed(p.now);
  p.step(100);
  TEST_ASSERT_EQUAL(Phase::Countdown, p.runner.phase());
  TEST_ASSERT_FALSE(p.runner.caught());
  TEST_ASSERT_EQUAL_UINT16(2, p.runner.round());
}
void test_start_ignored_while_playing() {
  Game c(Role::Chaser, 1);
  c.onStartPressed(0);
  for (uint32_t t = 10; t <= 3100; t += 10) c.tick(t);
  c.onMessage(status(2, Role::Runner, Phase::Playing, 1), 3100);
  c.onStartPressed(3100);
  TEST_ASSERT_EQUAL(Phase::Playing, c.phase());
  TEST_ASSERT_EQUAL_UINT16(1, c.round());
}
void test_idle_runner_not_counted() {
  Game c(Role::Chaser, 1);
  c.onStartPressed(0);
  for (uint32_t t = 10; t <= 3100; t += 10) c.tick(t);
  uint32_t t = 3100;
  c.onMessage(status(2, Role::Runner, Phase::Playing, 1), t);
  c.onMessage(status(4, Role::Runner, Phase::Idle, 0), t);  // 늦게 켜진 차: 대기
  Message caught = makeMessage(MsgType::Caught, 2, 1);
  caught.role = (uint8_t)Role::Runner;
  c.onMessage(caught, t + 10);
  c.onMessage(status(4, Role::Runner, Phase::Idle, 0), t + 10);
  c.tick(t + 20);
  TEST_ASSERT_EQUAL(Phase::Over, c.phase());
}
void test_runner_returns_idle_when_chaser_lost() {
  Game r(Role::Runner, 2);
  Message s = makeMessage(MsgType::Start, 1, 1);
  s.role = (uint8_t)Role::Chaser; s.ms = 0;
  r.onMessage(s, 0);
  r.tick(10);
  TEST_ASSERT_EQUAL(Phase::Playing, r.phase());
  r.tick(1990);
  TEST_ASSERT_EQUAL(Phase::Playing, r.phase());
  r.tick(2010);
  TEST_ASSERT_EQUAL(Phase::Idle, r.phase());
  TEST_ASSERT_TRUE(r.motorsEnabled(2010));
}
void test_chaser_returns_idle_when_no_runners() {
  Game c(Role::Chaser, 1);
  c.onStartPressed(0);
  for (uint32_t t = 10; t <= 3000; t += 10) c.tick(t);
  TEST_ASSERT_EQUAL(Phase::Playing, c.phase());
  for (uint32_t t = 3010; t <= 5100; t += 10) c.tick(t);
  TEST_ASSERT_EQUAL(Phase::Idle, c.phase());
}
void test_status_every_200ms() {
  Game g(Role::Runner, 2);
  int n = 0;
  for (uint32_t t = 0; t < 1000; t += 10) { g.tick(t); n += drain(g, MsgType::Status); }
  TEST_ASSERT_EQUAL_INT(5, n);
}
void test_ignores_own_messages() {
  Game r(Role::Runner, 2);
  Message s = makeMessage(MsgType::Start, 2, 1);
  s.role = (uint8_t)Role::Chaser;
  r.onMessage(s, 0);
  TEST_ASSERT_EQUAL(Phase::Idle, r.phase());
}

// 도주차를 판 1 "게임 중"으로 만듦 (추격차 상태 알림을 now 에 한 번)
static Game playingRunner(uint32_t now) {
  Game r(Role::Runner, 2);
  Message s = makeMessage(MsgType::Start, 1, 1);
  s.role = (uint8_t)Role::Chaser; s.ms = 0;
  r.onMessage(s, now);
  r.tick(now);
  return r;
}
void test_runner_over_returns_idle_when_chaser_lost() {
  Game r = playingRunner(0);
  r.onMessage(status(1, Role::Chaser, Phase::Over, 1), 100);
  TEST_ASSERT_EQUAL(Phase::Over, r.phase());
  r.tick(2050);
  TEST_ASSERT_EQUAL(Phase::Over, r.phase());
  r.tick(2100);
  TEST_ASSERT_EQUAL(Phase::Idle, r.phase());
}
void test_runner_missed_start_joins_countdown_from_status() {
  Game r = playingRunner(0);
  r.onHallTriggered(10);
  TEST_ASSERT_TRUE(r.caught());
  Message cs = status(1, Role::Chaser, Phase::Countdown, 2);  // START 는 못 받고 상태 알림만 받음
  cs.ms = 2500;
  r.onMessage(cs, 500);
  TEST_ASSERT_EQUAL(Phase::Countdown, r.phase());
  TEST_ASSERT_EQUAL_UINT16(2, r.round());
  TEST_ASSERT_FALSE(r.caught());
  Message later = status(1, Role::Chaser, Phase::Countdown, 2);  // 추격차가 계속 보임
  r.onMessage(later, 2500);
  r.tick(3000);
  TEST_ASSERT_EQUAL(Phase::Playing, r.phase());
}
void test_runner_other_round_playing_goes_idle() {
  Game r = playingRunner(0);
  r.onHallTriggered(10);
  drain(r, MsgType::Caught);  // 잡힌 순간 보낸 첫 CAUGHT 는 정상
  r.onMessage(status(1, Role::Chaser, Phase::Playing, 2), 500);
  TEST_ASSERT_EQUAL(Phase::Idle, r.phase());
  TEST_ASSERT_FALSE(r.caught());
  int n = 0;
  for (uint32_t t = 510; t <= 800; t += 10) { r.tick(t); n += drain(r, MsgType::Caught); }
  TEST_ASSERT_EQUAL_INT(0, n);  // 지난 판 CAUGHT 를 계속 보내지 않음
}
void test_runner_follows_chaser_idle() {
  Game r = playingRunner(0);
  r.onHallTriggered(10);
  drain(r, MsgType::Caught);  // 잡힌 순간 보낸 첫 CAUGHT 는 정상
  r.onMessage(status(1, Role::Chaser, Phase::Idle, 1), 500);
  TEST_ASSERT_EQUAL(Phase::Idle, r.phase());
  int n = 0;
  for (uint32_t t = 510; t <= 800; t += 10) { r.tick(t); n += drain(r, MsgType::Caught); }
  TEST_ASSERT_EQUAL_INT(0, n);
}

void setup() {
  delay(2000);
  UNITY_BEGIN();
  RUN_TEST(test_runner_over_returns_idle_when_chaser_lost);
  RUN_TEST(test_runner_missed_start_joins_countdown_from_status);
  RUN_TEST(test_runner_other_round_playing_goes_idle);
  RUN_TEST(test_runner_follows_chaser_idle);
  RUN_TEST(test_idle_allows_driving);
  RUN_TEST(test_runner_ignores_start_button);
  RUN_TEST(test_start_countdown_then_playing);
  RUN_TEST(test_start_repeats_for_300ms);
  RUN_TEST(test_runner_countdown_uses_remaining_ms);
  RUN_TEST(test_catch_stops_both_and_chaser_resumes_after_1s);
  RUN_TEST(test_caught_resent_until_ack);
  RUN_TEST(test_duplicate_caught_acked_once_counted);
  RUN_TEST(test_old_round_caught_ignored);
  RUN_TEST(test_all_caught_ends_game_and_start_restarts);
  RUN_TEST(test_start_ignored_while_playing);
  RUN_TEST(test_idle_runner_not_counted);
  RUN_TEST(test_runner_returns_idle_when_chaser_lost);
  RUN_TEST(test_chaser_returns_idle_when_no_runners);
  RUN_TEST(test_status_every_200ms);
  RUN_TEST(test_ignores_own_messages);
  UNITY_END();
}

void loop() {}

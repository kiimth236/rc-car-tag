#include <Arduino.h>
#include <string.h>
#include <unity.h>
#include "game.h"

using namespace rcar;

void setUp() {}
void tearDown() {}

// 차 최대 4대(추격 1번, 도주 2·3·4번)를 10ms마다 tick 하고 서로 메시지를 전달.
// put(id, nullptr) = 그 차를 뺌(꺼짐·재부팅). mute[id] = 그 차가 보내는 메시지만 사라짐(받기는 됨)
struct World {
  Game* car[5] = {};
  bool mute[5] = {};
  uint32_t lastSent[5] = {};  // 그 차가 마지막으로 메시지를 보낸(전달된) 시각
  uint32_t now = 0;
  void put(uint8_t id, Game* g) { car[id] = g; }
  void step(uint32_t ms) {
    for (uint32_t t = 0; t < ms; t += 10) {
      now += 10;
      for (uint8_t i = 1; i <= 4; i++) if (car[i]) car[i]->tick(now);
      Message out[64];
      int n = 0;
      for (uint8_t i = 1; i <= 4; i++) {
        if (!car[i]) continue;
        Message m;
        while (car[i]->popOutgoing(m)) {
          if (mute[i] || n == 64) continue;
          out[n++] = m;
          lastSent[i] = now;
        }
      }
      for (int k = 0; k < n; k++)
        for (uint8_t i = 1; i <= 4; i++)
          if (car[i] && i != out[k].from) car[i]->onMessage(out[k], now);
    }
  }
  void stepTo(uint32_t t) { if ((int32_t)(t - now) > 0) step(t - now); }
};

// 게임 대기 화면 + 폰 연결
static void lobby(Game& g) {
  g.setMode(UiMode::Lobby);
  g.setPhoneOk(true);
}
static constexpr uint8_t carBit(uint8_t id) { return (uint8_t)(1u << id); }

void test_new_car_starts_in_menu_motors_off() {
  Game r(Role::Runner, 2), c(Role::Chaser, 1);
  TEST_ASSERT_EQUAL(UiMode::Menu, r.mode());
  TEST_ASSERT_EQUAL(UiMode::Menu, c.mode());
  TEST_ASSERT_EQUAL(Phase::Idle, r.phase());
  TEST_ASSERT_FALSE(r.motorsEnabled(0));
  TEST_ASSERT_FALSE(c.motorsEnabled(0));
}
void test_select_from_solo_returns_menu() {
  Game r(Role::Runner, 2);
  TEST_ASSERT_TRUE(r.setMode(UiMode::Solo));
  TEST_ASSERT_TRUE(r.motorsEnabled(0));
  TEST_ASSERT_EQUAL(LedPattern::RoleSolid, r.led(0));
  r.onSelect(0);
  TEST_ASSERT_EQUAL(UiMode::Menu, r.mode());
  TEST_ASSERT_FALSE(r.motorsEnabled(0));
  TEST_ASSERT_EQUAL(LedPattern::IdleBlink, r.led(0));
}
void test_start_needs_participant() {
  World w;
  Game c(Role::Chaser, 1);
  lobby(c);
  w.put(1, &c);
  w.step(500);
  TEST_ASSERT_FALSE(c.onStartPressed(w.now));
  TEST_ASSERT_EQUAL(Phase::Idle, c.phase());
  TEST_ASSERT_EQUAL_UINT16(0, c.round());
}
void test_eligible_requires_lobby_and_phone() {
  World w;
  Game c(Role::Chaser, 1), r2(Role::Runner, 2), r3(Role::Runner, 3);
  lobby(c); lobby(r2);
  r3.setPhoneOk(true);  // 3번은 메뉴
  w.put(1, &c); w.put(2, &r2); w.put(3, &r3);
  w.step(500);
  TEST_ASSERT_EQUAL_UINT8(0b0100, c.eligibleMask(w.now));
  r3.setMode(UiMode::Lobby);
  r3.setPhoneOk(false);  // 대기 화면이지만 폰 끊김
  w.step(500);
  TEST_ASSERT_EQUAL_UINT8(0b0100, c.eligibleMask(w.now));
  TEST_ASSERT_EQUAL_UINT8(0b0100, c.participantsMask(w.now));
}
void test_toggle_excludes_and_new_runner_defaults_in() {
  World w;
  Game c(Role::Chaser, 1), r2(Role::Runner, 2), r3(Role::Runner, 3), r4(Role::Runner, 4);
  lobby(c); lobby(r2); lobby(r3);
  w.put(1, &c); w.put(2, &r2); w.put(3, &r3);
  w.step(500);
  TEST_ASSERT_EQUAL_UINT8(0b1100, c.participantsMask(w.now));
  c.toggleParticipant(2, w.now);
  TEST_ASSERT_EQUAL_UINT8(0b1000, c.participantsMask(w.now));
  lobby(r4);
  w.put(4, &r4);
  w.step(500);
  TEST_ASSERT_EQUAL_UINT8(0b11000, c.participantsMask(w.now));  // 새 차는 기본 참가
  c.toggleParticipant(3, w.now);
  TEST_ASSERT_EQUAL_UINT8(0b10000, c.participantsMask(w.now));
  c.toggleParticipant(3, w.now);
  TEST_ASSERT_EQUAL_UINT8(0b11000, c.participantsMask(w.now));
}
void test_start_ignored_in_countdown() {
  World w;
  Game c(Role::Chaser, 1), r2(Role::Runner, 2);
  lobby(c); lobby(r2);
  w.put(1, &c); w.put(2, &r2);
  w.step(500);
  TEST_ASSERT_TRUE(c.onStartPressed(w.now));
  TEST_ASSERT_FALSE(c.onStartPressed(w.now));
  w.step(100);
  TEST_ASSERT_FALSE(c.onStartPressed(w.now));
  TEST_ASSERT_EQUAL_UINT16(1, c.round());
  TEST_ASSERT_EQUAL_UINT16(1, r2.round());
  TEST_ASSERT_EQUAL(Phase::Countdown, c.phase());
}
void test_time_up_runners_win() {
  World w;
  Game c(Role::Chaser, 1), r2(Role::Runner, 2);
  lobby(c); lobby(r2);
  c.setGameSeconds(60);
  w.put(1, &c); w.put(2, &r2);
  w.step(500);
  TEST_ASSERT_TRUE(c.onStartPressed(w.now));
  uint32_t t0 = w.now;
  w.stepTo(t0 + 3000 + 60000 - 100);
  TEST_ASSERT_EQUAL(Phase::Playing, c.phase());
  TEST_ASSERT_EQUAL(Phase::Playing, r2.phase());
  w.stepTo(t0 + 3000 + 60000 + 300);
  TEST_ASSERT_EQUAL(Phase::Over, c.phase());
  TEST_ASSERT_EQUAL(Phase::Over, r2.phase());
  TEST_ASSERT_EQUAL(Winner::Runners, c.winner());
  TEST_ASSERT_EQUAL(Winner::Runners, r2.winner());
  TEST_ASSERT_EQUAL(LedPattern::RoleSolid, r2.led(w.now));
  TEST_ASSERT_EQUAL(LedPattern::Caught, c.led(w.now));
  TEST_ASSERT_FALSE(c.motorsEnabled(w.now));
  TEST_ASSERT_FALSE(r2.motorsEnabled(w.now));
}
void test_all_caught_chaser_wins_before_time() {
  World w;
  Game c(Role::Chaser, 1), r2(Role::Runner, 2), r3(Role::Runner, 3);
  lobby(c); lobby(r2); lobby(r3);
  w.put(1, &c); w.put(2, &r2); w.put(3, &r3);
  w.step(500);
  TEST_ASSERT_TRUE(c.onStartPressed(w.now));
  w.step(3500);
  r2.onHallTriggered(w.now);
  w.step(300);
  TEST_ASSERT_EQUAL(Phase::Playing, c.phase());  // 3번이 남음
  TEST_ASSERT_EQUAL_UINT8(carBit(2), c.caughtMask());
  r3.onHallTriggered(w.now);
  w.step(300);
  TEST_ASSERT_EQUAL(Phase::Over, c.phase());
  TEST_ASSERT_EQUAL(Winner::Chaser, c.winner());
  TEST_ASSERT_EQUAL(Phase::Over, r2.phase());
  TEST_ASSERT_EQUAL(Phase::Over, r3.phase());
  TEST_ASSERT_EQUAL(Winner::Chaser, r3.winner());
  TEST_ASSERT_EQUAL(LedPattern::ChaserWin, c.led(w.now));
  TEST_ASSERT_EQUAL(LedPattern::Caught, r2.led(w.now));
}
void test_offline_runner_caught_after_10s() {
  World w;
  Game c(Role::Chaser, 1), r2(Role::Runner, 2), r3(Role::Runner, 3);
  lobby(c); lobby(r2); lobby(r3);
  w.put(1, &c); w.put(2, &r2); w.put(3, &r3);
  w.step(500);
  TEST_ASSERT_TRUE(c.onStartPressed(w.now));
  w.step(3500);
  uint32_t off = w.lastSent[3];
  w.put(3, nullptr);  // 3번 꺼짐
  w.stepTo(off + 9900);
  TEST_ASSERT_EQUAL_UINT8(0, c.caughtMask());
  TEST_ASSERT_FALSE(c.takeBrake());
  w.stepTo(off + 10100);
  TEST_ASSERT_EQUAL_UINT8(carBit(3), c.caughtMask());
  TEST_ASSERT_FALSE(c.takeBrake());              // 꺼진 차 잡힘은 급정지 없음
  TEST_ASSERT_TRUE(c.motorsEnabled(w.now));      // 잠금도 없음
  TEST_ASSERT_EQUAL(Phase::Playing, c.phase());
  TEST_ASSERT_EQUAL_UINT8(0b1100, c.participantsMask(w.now));  // 참가 목록은 그대로
  r2.onHallTriggered(w.now);
  w.step(300);
  TEST_ASSERT_EQUAL(Phase::Over, c.phase());
  TEST_ASSERT_EQUAL(Winner::Chaser, c.winner());
}
void test_offline_caught_runner_adopts_caught() {
  World w;
  Game c(Role::Chaser, 1), r2(Role::Runner, 2), r3(Role::Runner, 3);
  lobby(c); lobby(r2); lobby(r3);
  w.put(1, &c); w.put(2, &r2); w.put(3, &r3);
  w.step(500);
  TEST_ASSERT_TRUE(c.onStartPressed(w.now));
  w.step(3500);
  r3.takeBrake();
  w.mute[3] = true;  // 3번은 살아 있지만 보내는 메시지가 추격차에 안 닿음
  w.step(10500);
  TEST_ASSERT_EQUAL_UINT8(carBit(3), c.caughtMask());
  TEST_ASSERT_TRUE(r3.caught());
  TEST_ASSERT_TRUE(r3.takeBrake());
  TEST_ASSERT_FALSE(r3.motorsEnabled(w.now));
  TEST_ASSERT_FALSE(r3.hallArmed());
  TEST_ASSERT_EQUAL(Phase::Playing, r3.phase());
}
void test_runner_reboot_rejoins_playing() {
  World w;
  Game c(Role::Chaser, 1), r2(Role::Runner, 2), r3(Role::Runner, 3);
  lobby(c); lobby(r2); lobby(r3);
  w.put(1, &c); w.put(2, &r2); w.put(3, &r3);
  w.step(500);
  TEST_ASSERT_TRUE(c.onStartPressed(w.now));
  w.step(4000);
  Game r2b(Role::Runner, 2);  // 재부팅: 메뉴, 대기
  w.put(2, &r2b);
  TEST_ASSERT_FALSE(r2b.takeRejoined());
  w.step(300);
  TEST_ASSERT_EQUAL(UiMode::Lobby, r2b.mode());
  TEST_ASSERT_EQUAL(Phase::Playing, r2b.phase());
  TEST_ASSERT_EQUAL_UINT16(1, r2b.round());
  TEST_ASSERT_FALSE(r2b.caught());
  TEST_ASSERT_FALSE(r2b.spectating());
  TEST_ASSERT_TRUE(r2b.motorsEnabled(w.now));
  TEST_ASSERT_TRUE(r2b.hallArmed());
  TEST_ASSERT_TRUE(r2b.takeRejoined());
  TEST_ASSERT_FALSE(r2b.takeRejoined());  // 한 번만
  w.step(500);
  TEST_ASSERT_FALSE(r2b.takeRejoined());
  TEST_ASSERT_EQUAL(Phase::Playing, c.phase());
  TEST_ASSERT_EQUAL_UINT8(0b1100, c.participantsMask(w.now));
  TEST_ASSERT_EQUAL_UINT8(0, c.caughtMask());
}
void test_runner_reboot_rejoins_caught() {
  World w;
  Game c(Role::Chaser, 1), r2(Role::Runner, 2), r3(Role::Runner, 3);
  lobby(c); lobby(r2); lobby(r3);
  w.put(1, &c); w.put(2, &r2); w.put(3, &r3);
  w.step(500);
  TEST_ASSERT_TRUE(c.onStartPressed(w.now));
  w.step(3500);
  r3.onHallTriggered(w.now);
  w.step(300);
  TEST_ASSERT_EQUAL_UINT8(carBit(3), c.caughtMask());
  Game r3b(Role::Runner, 3);
  w.put(3, &r3b);
  w.step(300);
  TEST_ASSERT_EQUAL(Phase::Playing, r3b.phase());
  TEST_ASSERT_EQUAL(UiMode::Lobby, r3b.mode());
  TEST_ASSERT_TRUE(r3b.caught());
  TEST_ASSERT_FALSE(r3b.motorsEnabled(w.now));
  TEST_ASSERT_FALSE(r3b.hallArmed());
  TEST_ASSERT_TRUE(r3b.takeRejoined());
  TEST_ASSERT_EQUAL(LedPattern::Caught, r3b.led(w.now));
  TEST_ASSERT_EQUAL(Phase::Playing, c.phase());  // 2번이 남음
}
void test_lobby_runner_midround_becomes_spectator() {
  World w;
  Game c(Role::Chaser, 1), r2(Role::Runner, 2), r4(Role::Runner, 4);
  lobby(c); lobby(r2); lobby(r4);
  w.put(1, &c); w.put(2, &r2); w.put(4, &r4);
  w.step(500);
  c.toggleParticipant(4, w.now);  // 4번 빼기
  TEST_ASSERT_TRUE(c.onStartPressed(w.now));
  w.step(300);
  TEST_ASSERT_TRUE(r4.spectating());
  TEST_ASSERT_EQUAL(Phase::Countdown, r4.phase());
  TEST_ASSERT_FALSE(r4.motorsEnabled(w.now));
  w.step(3500);
  TEST_ASSERT_EQUAL(Phase::Playing, r4.phase());
  TEST_ASSERT_FALSE(r4.motorsEnabled(w.now));
  TEST_ASSERT_FALSE(r4.hallArmed());
  TEST_ASSERT_EQUAL(LedPattern::IdleBlink, r4.led(w.now));
  r4.onSelect(w.now);
  TEST_ASSERT_FALSE(r4.setMode(UiMode::Solo));
  TEST_ASSERT_FALSE(r4.setMode(UiMode::Menu));
  TEST_ASSERT_FALSE(r4.onStartPressed(w.now));
  TEST_ASSERT_EQUAL(UiMode::Lobby, r4.mode());
  TEST_ASSERT_TRUE(r4.spectating());
  TEST_ASSERT_EQUAL(Phase::Playing, r4.phase());
  r2.onHallTriggered(w.now);
  w.step(300);
  TEST_ASSERT_EQUAL(Phase::Over, r4.phase());
  TEST_ASSERT_EQUAL(Winner::Chaser, r4.winner());
  r4.onSelect(w.now);
  TEST_ASSERT_EQUAL(UiMode::Lobby, r4.mode());
  TEST_ASSERT_EQUAL(Phase::Idle, r4.phase());
  TEST_ASSERT_FALSE(r4.spectating());
  TEST_ASSERT_TRUE(r4.motorsEnabled(w.now));
}
void test_menu_runner_becomes_spectator() {
  World w;
  Game c(Role::Chaser, 1), r2(Role::Runner, 2), r4(Role::Runner, 4);
  lobby(c); lobby(r2);
  r4.setPhoneOk(true);  // 4번은 메뉴
  w.put(1, &c); w.put(2, &r2); w.put(4, &r4);
  w.step(500);
  TEST_ASSERT_TRUE(c.onStartPressed(w.now));
  w.step(200);
  TEST_ASSERT_EQUAL(UiMode::Lobby, r4.mode());
  TEST_ASSERT_TRUE(r4.spectating());
  TEST_ASSERT_FALSE(r4.motorsEnabled(w.now));
  TEST_ASSERT_EQUAL_UINT8(carBit(2), c.participantsMask(w.now));
}
void test_solo_runner_leaving_solo_midround_spectates() {
  World w;
  Game c(Role::Chaser, 1), r2(Role::Runner, 2), r4(Role::Runner, 4);
  lobby(c); lobby(r2);
  r4.setMode(UiMode::Solo); r4.setPhoneOk(true);
  w.put(1, &c); w.put(2, &r2); w.put(4, &r4);
  w.step(500);
  TEST_ASSERT_TRUE(c.onStartPressed(w.now));
  w.step(3500);
  TEST_ASSERT_FALSE(r4.spectating());
  TEST_ASSERT_TRUE(r4.motorsEnabled(w.now));
  r4.onSelect(w.now);
  TEST_ASSERT_EQUAL(UiMode::Menu, r4.mode());
  w.step(300);
  TEST_ASSERT_TRUE(r4.spectating());
  TEST_ASSERT_EQUAL(UiMode::Lobby, r4.mode());
  TEST_ASSERT_EQUAL(Phase::Playing, r4.phase());
  TEST_ASSERT_FALSE(r4.motorsEnabled(w.now));
}
void test_solo_runner_in_start_bit_forced_lobby() {
  World w;
  Game c(Role::Chaser, 1), r2(Role::Runner, 2);
  lobby(c); lobby(r2);
  w.put(1, &c); w.put(2, &r2);
  w.step(500);
  TEST_ASSERT_TRUE(r2.setMode(UiMode::Solo));  // START 바로 전에 혼자 놀기로
  TEST_ASSERT_TRUE(c.onStartPressed(w.now));
  TEST_ASSERT_EQUAL_UINT8(carBit(2), c.participantsMask(w.now));
  w.step(100);
  TEST_ASSERT_EQUAL(UiMode::Lobby, r2.mode());
  TEST_ASSERT_EQUAL(Phase::Countdown, r2.phase());
  TEST_ASSERT_FALSE(r2.spectating());
  TEST_ASSERT_FALSE(r2.motorsEnabled(w.now));
}
void test_solo_runner_ignores_round() {
  World w;
  Game c(Role::Chaser, 1), r2(Role::Runner, 2), r4(Role::Runner, 4);
  lobby(c); lobby(r2);
  r4.setMode(UiMode::Solo); r4.setPhoneOk(true);
  w.put(1, &c); w.put(2, &r2); w.put(4, &r4);
  w.step(500);
  TEST_ASSERT_EQUAL_UINT8(carBit(2), c.eligibleMask(w.now));  // 혼자 놀기 차는 고를 수 없음
  TEST_ASSERT_TRUE(c.onStartPressed(w.now));
  w.step(200);
  TEST_ASSERT_EQUAL(Phase::Idle, r4.phase());
  TEST_ASSERT_TRUE(r4.motorsEnabled(w.now));
  w.step(3500);
  TEST_ASSERT_EQUAL(Phase::Idle, r4.phase());
  TEST_ASSERT_EQUAL(UiMode::Solo, r4.mode());
  TEST_ASSERT_FALSE(r4.spectating());
  TEST_ASSERT_TRUE(r4.motorsEnabled(w.now));
  TEST_ASSERT_EQUAL(LedPattern::RoleSolid, r4.led(w.now));
}
void test_participant_cannot_leave_lobby_midround() {
  World w;
  Game c(Role::Chaser, 1), r2(Role::Runner, 2);
  lobby(c); lobby(r2);
  w.put(1, &c); w.put(2, &r2);
  w.step(500);
  TEST_ASSERT_TRUE(c.onStartPressed(w.now));
  w.step(500);
  TEST_ASSERT_FALSE(r2.setMode(UiMode::Menu));
  r2.onSelect(w.now);
  TEST_ASSERT_EQUAL(UiMode::Lobby, r2.mode());
  TEST_ASSERT_EQUAL(Phase::Countdown, r2.phase());
  w.step(3000);
  TEST_ASSERT_FALSE(r2.setMode(UiMode::Solo));
  r2.onSelect(w.now);
  TEST_ASSERT_EQUAL(UiMode::Lobby, r2.mode());
  TEST_ASSERT_EQUAL(Phase::Playing, r2.phase());
  TEST_ASSERT_FALSE(c.setMode(UiMode::Menu));
  c.onSelect(w.now);
  TEST_ASSERT_EQUAL(UiMode::Lobby, c.mode());
  TEST_ASSERT_EQUAL(Phase::Playing, c.phase());
}
void test_over_select_returns_lobby_idle() {
  World w;
  Game c(Role::Chaser, 1), r2(Role::Runner, 2);
  lobby(c); lobby(r2);
  w.put(1, &c); w.put(2, &r2);
  w.step(500);
  TEST_ASSERT_TRUE(c.onStartPressed(w.now));
  w.step(3500);
  r2.onHallTriggered(w.now);
  w.step(300);
  TEST_ASSERT_EQUAL(Phase::Over, c.phase());
  c.onSelect(w.now);
  TEST_ASSERT_EQUAL(Phase::Idle, c.phase());
  TEST_ASSERT_EQUAL(UiMode::Lobby, c.mode());
  TEST_ASSERT_TRUE(c.motorsEnabled(w.now));
  w.step(300);
  TEST_ASSERT_EQUAL(Phase::Idle, r2.phase());  // 추격차가 대기로 → 도주차도 대기
  TEST_ASSERT_FALSE(r2.caught());
}
void test_restart_keeps_game_seconds() {
  World w;
  Game c(Role::Chaser, 1), r2(Role::Runner, 2);
  lobby(c); lobby(r2);
  c.setGameSeconds(120);
  w.put(1, &c); w.put(2, &r2);
  w.step(500);
  TEST_ASSERT_TRUE(c.onStartPressed(w.now));
  w.step(3500);
  r2.onHallTriggered(w.now);
  w.step(300);
  TEST_ASSERT_EQUAL(Phase::Over, c.phase());
  TEST_ASSERT_TRUE(c.onStartPressed(w.now));
  TEST_ASSERT_EQUAL_UINT16(120, c.gameSeconds());
  TEST_ASSERT_EQUAL_UINT16(2, c.round());
  TEST_ASSERT_EQUAL(Winner::None, c.winner());
}
void test_restart_uses_all_eligible() {
  World w;
  Game c(Role::Chaser, 1), r2(Role::Runner, 2), r3(Role::Runner, 3);
  lobby(c); lobby(r2); lobby(r3);
  w.put(1, &c); w.put(2, &r2); w.put(3, &r3);
  w.step(500);
  c.toggleParticipant(2, w.now);
  TEST_ASSERT_TRUE(c.onStartPressed(w.now));
  w.step(3500);
  r3.onHallTriggered(w.now);
  w.step(300);
  TEST_ASSERT_EQUAL(Phase::Over, c.phase());
  TEST_ASSERT_EQUAL_UINT8(0b1000, c.participantsMask(w.now));  // 결과 화면은 지난 판
  TEST_ASSERT_TRUE(c.onStartPressed(w.now));  // 다시 하기
  TEST_ASSERT_EQUAL_UINT8(0b1100, c.participantsMask(w.now));
  w.step(100);
  TEST_ASSERT_EQUAL(Phase::Countdown, r2.phase());
  TEST_ASSERT_FALSE(r2.spectating());
  // 다시 하기는 제외 목록도 비움 → 다음 대기 화면에서 2번은 기본 참가
  w.step(3500);
  r2.onHallTriggered(w.now);
  r3.onHallTriggered(w.now);
  w.step(300);
  TEST_ASSERT_EQUAL(Phase::Over, c.phase());
  c.onSelect(w.now);
  TEST_ASSERT_EQUAL(Phase::Idle, c.phase());
  TEST_ASSERT_EQUAL_UINT8(0b1100, c.participantsMask(w.now));
}
void test_runner_follows_chaser_game_seconds() {
  World w;
  Game c(Role::Chaser, 1), r2(Role::Runner, 2);
  lobby(c); lobby(r2);
  c.setGameSeconds(60);
  r2.setGameSeconds(180);
  w.put(1, &c); w.put(2, &r2);
  w.step(500);
  TEST_ASSERT_EQUAL_UINT16(60, r2.gameSeconds());
  c.setGameSeconds(stepGameSeconds(c.gameSeconds(), +1));
  w.step(300);
  TEST_ASSERT_EQUAL_UINT16(90, r2.gameSeconds());
}
void test_runner_msleft_synced() {
  World w;
  Game c(Role::Chaser, 1), r2(Role::Runner, 2);
  lobby(c); lobby(r2);
  c.setGameSeconds(60);
  w.put(1, &c); w.put(2, &r2);
  w.step(500);
  TEST_ASSERT_TRUE(c.onStartPressed(w.now));
  uint32_t t0 = w.now;
  w.step(3500);
  for (int i = 0; i < 20; i++) {
    if (i == 10) {  // 도주차 시계가 어긋난 것처럼 엉뚱한 남은 시간을 한 번 받음 → 그 값을 따르고, 다음 상태로 다시 맞춤
      Message skew = makeMessage(MsgType::Status, 1, 1);
      skew.role = (uint8_t)Role::Chaser; skew.phase = (uint8_t)Phase::Playing;
      skew.participants = carBit(2); skew.gameSec = 60; skew.msLeft = 12345;
      r2.onMessage(skew, w.now);
      TEST_ASSERT_EQUAL_UINT32(12345, r2.msLeft(w.now));
    }
    w.step(2730);
    int32_t d = (int32_t)r2.msLeft(w.now) - (int32_t)c.msLeft(w.now);
    TEST_ASSERT_TRUE(d >= -50 && d <= 50);
    TEST_ASSERT_EQUAL(Phase::Playing, r2.phase());
  }
  // 끝나기 1초 전부터 추격차 메시지가 안 닿음 → 도주차는 남은 시간 0에서 멈추고 스스로 끝내지 않음
  w.stepTo(t0 + 3000 + 59000);
  w.mute[1] = true;
  w.stepTo(t0 + 3000 + 60000 + 500);
  TEST_ASSERT_EQUAL_UINT32(0, r2.msLeft(w.now));
  TEST_ASSERT_EQUAL(Phase::Playing, r2.phase());
  TEST_ASSERT_EQUAL(Phase::Over, c.phase());
  w.mute[1] = false;
  w.step(300);
  TEST_ASSERT_EQUAL(Phase::Over, r2.phase());
  TEST_ASSERT_EQUAL(Winner::Runners, r2.winner());
}
void test_peer_name_shared_and_bad_name_dropped() {
  static const char* kMinsu = "\xeb\xaf\xbc\xec\x88\x98";  // "민수"
  World w;
  Game c(Role::Chaser, 1), r2(Role::Runner, 2);
  r2.setName(kMinsu);
  TEST_ASSERT_EQUAL_STRING(kMinsu, r2.name());
  r2.setName("abc");  // 3글자는 거부
  TEST_ASSERT_EQUAL_STRING(kMinsu, r2.name());
  TEST_ASSERT_EQUAL_STRING("", c.peerName(2));
  w.put(1, &c); w.put(2, &r2);
  w.step(300);
  TEST_ASSERT_EQUAL_STRING(kMinsu, c.peerName(2));
  Message bad = makeMessage(MsgType::Status, 2, 0);
  bad.role = (uint8_t)Role::Runner;
  memcpy(bad.name, "abc", 3);
  c.onMessage(bad, w.now);
  TEST_ASSERT_EQUAL_STRING(kMinsu, c.peerName(2));
  Message full = makeMessage(MsgType::Status, 2, 0);  // 8바이트 꽉 참 (끝 0 없음)
  full.role = (uint8_t)Role::Runner;
  memcpy(full.name, "ABCDEFGH", 8);
  c.onMessage(full, w.now);
  TEST_ASSERT_EQUAL_STRING(kMinsu, c.peerName(2));
  TEST_ASSERT_EQUAL_STRING("", c.peerName(0));
  TEST_ASSERT_EQUAL_STRING("", c.peerName(5));
  TEST_ASSERT_EQUAL_STRING("", c.peerName(3));
}
void test_msleft_counts_down() {
  World w;
  Game c(Role::Chaser, 1), r2(Role::Runner, 2);
  lobby(c); lobby(r2);
  w.put(1, &c); w.put(2, &r2);
  w.step(500);
  TEST_ASSERT_EQUAL_UINT32(0, c.msLeft(w.now));
  TEST_ASSERT_TRUE(c.onStartPressed(w.now));
  uint32_t t0 = w.now;
  TEST_ASSERT_TRUE(c.msLeft(w.now) <= 3000 && c.msLeft(w.now) > 2900);
  w.step(1000);
  TEST_ASSERT_UINT32_WITHIN(20, 2000, c.msLeft(w.now));
  w.stepTo(t0 + 3000);
  TEST_ASSERT_EQUAL(Phase::Playing, c.phase());
  TEST_ASSERT_UINT32_WITHIN(20, 180000, c.msLeft(w.now));
  w.step(1000);
  TEST_ASSERT_UINT32_WITHIN(20, 179000, c.msLeft(w.now));
  TEST_ASSERT_UINT32_WITHIN(50, 179000, r2.msLeft(w.now));
}
void test_round_running_nearby_for_solo_car() {
  World w;
  Game c(Role::Chaser, 1), r2(Role::Runner, 2), r4(Role::Runner, 4);
  lobby(c); lobby(r2);
  r4.setMode(UiMode::Solo);
  w.put(1, &c); w.put(2, &r2); w.put(4, &r4);
  w.step(500);
  TEST_ASSERT_FALSE(r4.roundRunningNearby(w.now));
  TEST_ASSERT_FALSE(c.roundRunningNearby(w.now));
  TEST_ASSERT_TRUE(c.onStartPressed(w.now));
  TEST_ASSERT_TRUE(c.roundRunningNearby(w.now));
  w.step(200);
  TEST_ASSERT_TRUE(r4.roundRunningNearby(w.now));
  w.step(3500);
  TEST_ASSERT_TRUE(r4.roundRunningNearby(w.now));
  r2.onHallTriggered(w.now);
  w.step(300);
  TEST_ASSERT_EQUAL(Phase::Over, c.phase());
  TEST_ASSERT_FALSE(r4.roundRunningNearby(w.now));
  TEST_ASSERT_FALSE(c.roundRunningNearby(w.now));
}
void test_step_game_seconds_clamps() {
  TEST_ASSERT_EQUAL_UINT16(210, stepGameSeconds(180, +1));
  TEST_ASSERT_EQUAL_UINT16(150, stepGameSeconds(180, -1));
  TEST_ASSERT_EQUAL_UINT16(300, stepGameSeconds(300, +1));
  TEST_ASSERT_EQUAL_UINT16(60, stepGameSeconds(60, -1));
  TEST_ASSERT_EQUAL_UINT16(60, stepGameSeconds(75, -1));
  TEST_ASSERT_EQUAL_UINT16(60, stepGameSeconds(75, 0));
  TEST_ASSERT_EQUAL_UINT16(60, stepGameSeconds(0, 0));
  TEST_ASSERT_EQUAL_UINT16(300, stepGameSeconds(999, 0));
  Game c(Role::Chaser, 1);
  TEST_ASSERT_EQUAL_UINT16(180, c.gameSeconds());
  c.setGameSeconds(999);
  TEST_ASSERT_EQUAL_UINT16(300, c.gameSeconds());
  c.setGameSeconds(0);
  TEST_ASSERT_EQUAL_UINT16(60, c.gameSeconds());
}
void test_runner_dismissed_over_stays_idle() {
  World w;
  Game c(Role::Chaser, 1), r2(Role::Runner, 2);
  lobby(c); lobby(r2);
  w.put(1, &c); w.put(2, &r2);
  w.step(500);
  TEST_ASSERT_TRUE(c.onStartPressed(w.now));
  w.step(3500);
  r2.onHallTriggered(w.now);
  w.step(300);
  TEST_ASSERT_EQUAL(Phase::Over, r2.phase());
  r2.onSelect(w.now);
  TEST_ASSERT_EQUAL(Phase::Idle, r2.phase());
  TEST_ASSERT_EQUAL(UiMode::Lobby, r2.mode());
  TEST_ASSERT_FALSE(r2.caught());
  w.step(1000);
  TEST_ASSERT_EQUAL(Phase::Over, c.phase());
  TEST_ASSERT_EQUAL(Phase::Idle, r2.phase());
  TEST_ASSERT_TRUE(r2.motorsEnabled(w.now));
}

void setup() {
  delay(2000);
  UNITY_BEGIN();
  RUN_TEST(test_new_car_starts_in_menu_motors_off);
  RUN_TEST(test_select_from_solo_returns_menu);
  RUN_TEST(test_start_needs_participant);
  RUN_TEST(test_eligible_requires_lobby_and_phone);
  RUN_TEST(test_toggle_excludes_and_new_runner_defaults_in);
  RUN_TEST(test_start_ignored_in_countdown);
  RUN_TEST(test_time_up_runners_win);
  RUN_TEST(test_all_caught_chaser_wins_before_time);
  RUN_TEST(test_offline_runner_caught_after_10s);
  RUN_TEST(test_offline_caught_runner_adopts_caught);
  RUN_TEST(test_runner_reboot_rejoins_playing);
  RUN_TEST(test_runner_reboot_rejoins_caught);
  RUN_TEST(test_lobby_runner_midround_becomes_spectator);
  RUN_TEST(test_menu_runner_becomes_spectator);
  RUN_TEST(test_solo_runner_leaving_solo_midround_spectates);
  RUN_TEST(test_solo_runner_in_start_bit_forced_lobby);
  RUN_TEST(test_solo_runner_ignores_round);
  RUN_TEST(test_participant_cannot_leave_lobby_midround);
  RUN_TEST(test_over_select_returns_lobby_idle);
  RUN_TEST(test_restart_keeps_game_seconds);
  RUN_TEST(test_restart_uses_all_eligible);
  RUN_TEST(test_runner_follows_chaser_game_seconds);
  RUN_TEST(test_runner_msleft_synced);
  RUN_TEST(test_peer_name_shared_and_bad_name_dropped);
  RUN_TEST(test_msleft_counts_down);
  RUN_TEST(test_round_running_nearby_for_solo_car);
  RUN_TEST(test_step_game_seconds_clamps);
  RUN_TEST(test_runner_dismissed_over_stays_idle);
  UNITY_END();
}

void loop() {}

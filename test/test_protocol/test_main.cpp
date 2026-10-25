#include <Arduino.h>
#include <unity.h>
#include <string.h>
#include "protocol.h"

using namespace rcar;

void setUp() {}
void tearDown() {}

void test_size_is_30() { TEST_ASSERT_EQUAL_UINT(30, sizeof(Message)); }

void test_roundtrip() {
  Message m = makeMessage(MsgType::Caught, 3, 7);
  m.target = 1; m.battDeciV = 74; m.msLeft = 2500;
  uint8_t buf[sizeof(Message)];
  TEST_ASSERT_EQUAL_UINT(30, encode(m, buf, sizeof buf));
  Message o;
  TEST_ASSERT_TRUE(decode(buf, sizeof(Message), kTeamMagic, o));
  TEST_ASSERT_EQUAL_UINT8((uint8_t)MsgType::Caught, o.type);
  TEST_ASSERT_EQUAL_UINT8(3, o.from);
  TEST_ASSERT_EQUAL_UINT16(7, o.round);
  TEST_ASSERT_EQUAL_UINT8(1, o.target);
  TEST_ASSERT_EQUAL_UINT8(74, o.battDeciV);
  TEST_ASSERT_EQUAL_UINT32(2500, o.msLeft);
}
void test_encode_small_buffer() {
  Message m = makeMessage(MsgType::Status, 1, 0);
  uint8_t buf[8];
  TEST_ASSERT_EQUAL_UINT(0, encode(m, buf, sizeof buf));
}
void test_decode_rejects_other_team() {
  Message m = makeMessage(MsgType::Status, 1, 0);
  m.magic = 0x1234;
  uint8_t buf[sizeof(Message)]; encode(m, buf, sizeof buf);
  Message o;
  TEST_ASSERT_FALSE(decode(buf, sizeof(Message), kTeamMagic, o));
}
void test_decode_rejects_bad_length() {
  Message m = makeMessage(MsgType::Status, 1, 0);
  uint8_t buf[40] = {0}; encode(m, buf, sizeof buf);
  Message o;
  TEST_ASSERT_FALSE(decode(buf, 29, kTeamMagic, o));
  TEST_ASSERT_FALSE(decode(buf, 31, kTeamMagic, o));
}
void test_decode_rejects_bad_fields() {
  Message o;
  uint8_t buf[sizeof(Message)];
  Message a = makeMessage(MsgType::Status, 5, 0);  // 차 번호 5 없음
  encode(a, buf, sizeof buf);
  TEST_ASSERT_FALSE(decode(buf, sizeof(Message), kTeamMagic, o));
  Message b = makeMessage(MsgType::Status, 1, 0);
  b.type = 9;
  encode(b, buf, sizeof buf);
  TEST_ASSERT_FALSE(decode(buf, sizeof(Message), kTeamMagic, o));
  Message c = makeMessage(MsgType::Status, 1, 0);
  c.phase = 7;
  encode(c, buf, sizeof buf);
  TEST_ASSERT_FALSE(decode(buf, sizeof(Message), kTeamMagic, o));
}

void test_v2_roundtrip_all_fields() {
  Message m = makeMessage(MsgType::Status, 2, 9);
  m.mode = (uint8_t)UiMode::Lobby; m.phoneOk = 1;
  m.participants = 0b01100; m.caughtMask = 0b00100;
  m.winner = (uint8_t)Winner::Runners; m.gameSec = 180; m.msLeft = 157000;
  memcpy(m.name, "ë¯¼ì", 6);  // "민수"
  uint8_t buf[sizeof(Message)];
  TEST_ASSERT_EQUAL_UINT(30, encode(m, buf, sizeof buf));
  Message o;
  TEST_ASSERT_TRUE(decode(buf, sizeof buf, kTeamMagic, o));
  TEST_ASSERT_EQUAL_UINT8((uint8_t)UiMode::Lobby, o.mode);
  TEST_ASSERT_EQUAL_UINT8(1, o.phoneOk);
  TEST_ASSERT_EQUAL_UINT8(0b01100, o.participants);
  TEST_ASSERT_EQUAL_UINT8(0b00100, o.caughtMask);
  TEST_ASSERT_EQUAL_UINT8((uint8_t)Winner::Runners, o.winner);
  TEST_ASSERT_EQUAL_UINT16(180, o.gameSec);
  TEST_ASSERT_EQUAL_UINT32(157000, o.msLeft);
  TEST_ASSERT_EQUAL_MEMORY(m.name, o.name, 8);
}
void test_old_magic_rejected() {
  Message m = makeMessage(MsgType::Status, 1, 0);
  m.magic = 0x52C4;
  uint8_t buf[sizeof(Message)]; encode(m, buf, sizeof buf);
  Message o;
  TEST_ASSERT_FALSE(decode(buf, sizeof buf, kTeamMagic, o));
}
void test_bad_mode_or_winner_rejected() {
  Message o;
  uint8_t buf[sizeof(Message)];
  Message a = makeMessage(MsgType::Status, 1, 0);
  a.mode = 4;
  encode(a, buf, sizeof buf);
  TEST_ASSERT_FALSE(decode(buf, sizeof buf, kTeamMagic, o));
  Message b = makeMessage(MsgType::Status, 1, 0);
  b.winner = 3;
  encode(b, buf, sizeof buf);
  TEST_ASSERT_FALSE(decode(buf, sizeof buf, kTeamMagic, o));
}

void setup() {
  delay(2000);
  UNITY_BEGIN();
  RUN_TEST(test_size_is_30);
  RUN_TEST(test_roundtrip);
  RUN_TEST(test_encode_small_buffer);
  RUN_TEST(test_decode_rejects_other_team);
  RUN_TEST(test_decode_rejects_bad_length);
  RUN_TEST(test_decode_rejects_bad_fields);
  RUN_TEST(test_v2_roundtrip_all_fields);
  RUN_TEST(test_old_magic_rejected);
  RUN_TEST(test_bad_mode_or_winner_rejected);
  UNITY_END();
}

void loop() {}

#include <Arduino.h>
#include <unity.h>
#include "protocol.h"

using namespace rcar;

void setUp() {}
void tearDown() {}

void test_size_is_13() { TEST_ASSERT_EQUAL_UINT(13, sizeof(Message)); }

void test_roundtrip() {
  Message m = makeMessage(MsgType::Caught, 3, 7);
  m.target = 1; m.battDeciV = 74; m.ms = 2500;
  uint8_t buf[32];
  TEST_ASSERT_EQUAL_UINT(13, encode(m, buf, sizeof buf));
  Message o;
  TEST_ASSERT_TRUE(decode(buf, 13, kTeamMagic, o));
  TEST_ASSERT_EQUAL_UINT8((uint8_t)MsgType::Caught, o.type);
  TEST_ASSERT_EQUAL_UINT8(3, o.from);
  TEST_ASSERT_EQUAL_UINT16(7, o.round);
  TEST_ASSERT_EQUAL_UINT8(1, o.target);
  TEST_ASSERT_EQUAL_UINT8(74, o.battDeciV);
  TEST_ASSERT_EQUAL_UINT16(2500, o.ms);
}
void test_encode_small_buffer() {
  Message m = makeMessage(MsgType::Status, 1, 0);
  uint8_t buf[8];
  TEST_ASSERT_EQUAL_UINT(0, encode(m, buf, sizeof buf));
}
void test_decode_rejects_other_team() {
  Message m = makeMessage(MsgType::Status, 1, 0);
  m.magic = 0x1234;
  uint8_t buf[13]; encode(m, buf, sizeof buf);
  Message o;
  TEST_ASSERT_FALSE(decode(buf, 13, kTeamMagic, o));
}
void test_decode_rejects_bad_length() {
  Message m = makeMessage(MsgType::Status, 1, 0);
  uint8_t buf[16] = {0}; encode(m, buf, sizeof buf);
  Message o;
  TEST_ASSERT_FALSE(decode(buf, 12, kTeamMagic, o));
  TEST_ASSERT_FALSE(decode(buf, 14, kTeamMagic, o));
}
void test_decode_rejects_bad_fields() {
  Message o;
  uint8_t buf[13];
  Message a = makeMessage(MsgType::Status, 5, 0);  // 차 번호 5 없음
  encode(a, buf, sizeof buf);
  TEST_ASSERT_FALSE(decode(buf, 13, kTeamMagic, o));
  Message b = makeMessage(MsgType::Status, 1, 0);
  b.type = 9;
  encode(b, buf, sizeof buf);
  TEST_ASSERT_FALSE(decode(buf, 13, kTeamMagic, o));
  Message c = makeMessage(MsgType::Status, 1, 0);
  c.phase = 7;
  encode(c, buf, sizeof buf);
  TEST_ASSERT_FALSE(decode(buf, 13, kTeamMagic, o));
}

void setup() {
  delay(2000);
  UNITY_BEGIN();
  RUN_TEST(test_size_is_13);
  RUN_TEST(test_roundtrip);
  RUN_TEST(test_encode_small_buffer);
  RUN_TEST(test_decode_rejects_other_team);
  RUN_TEST(test_decode_rejects_bad_length);
  RUN_TEST(test_decode_rejects_bad_fields);
  UNITY_END();
}

void loop() {}

#include <Arduino.h>
#include <unity.h>
#include <string.h>
#include "web_proto.h"

using namespace rcar;

void setUp() {}
void tearDown() {}

static bool parse(const char* s, WebCmd& c) { return parseWebCmd(s, strlen(s), c); }

void test_parse_joy() {
  WebCmd c;
  TEST_ASSERT_TRUE(parse("J 50 -100", c));
  TEST_ASSERT_TRUE(c.kind == WebCmd::Kind::Joy);
  TEST_ASSERT_EQUAL_INT(50, c.x);
  TEST_ASSERT_EQUAL_INT(-100, c.y);
  TEST_ASSERT_TRUE(parse("J 300 0", c));
  TEST_ASSERT_EQUAL_INT(100, c.x);
  TEST_ASSERT_TRUE(parse("J 0 -999", c));
  TEST_ASSERT_EQUAL_INT(-100, c.y);
  TEST_ASSERT_FALSE(parse("J 5", c));
  TEST_ASSERT_FALSE(parse("J a b", c));
  TEST_ASSERT_FALSE(parse("J 5 6 7", c));
}
void test_parse_buttons() {
  WebCmd c;
  TEST_ASSERT_TRUE(parse("S", c));
  TEST_ASSERT_TRUE(c.kind == WebCmd::Kind::Select);
  TEST_ASSERT_TRUE(parse("G", c));
  TEST_ASSERT_TRUE(c.kind == WebCmd::Kind::Start);
  TEST_ASSERT_TRUE(parse("T +", c));
  TEST_ASSERT_TRUE(c.kind == WebCmd::Kind::Time);
  TEST_ASSERT_EQUAL_INT(1, c.dir);
  TEST_ASSERT_TRUE(parse("T -", c));
  TEST_ASSERT_EQUAL_INT(-1, c.dir);
  TEST_ASSERT_FALSE(parse("T x", c));
  TEST_ASSERT_FALSE(parse("", c));
  TEST_ASSERT_FALSE(parse("Z", c));
}
void test_parse_mode() {
  WebCmd c;
  TEST_ASSERT_TRUE(parse("M solo", c));
  TEST_ASSERT_TRUE(c.kind == WebCmd::Kind::Mode);
  TEST_ASSERT_TRUE(c.mode == UiMode::Solo);
  TEST_ASSERT_TRUE(parse("M menu", c));
  TEST_ASSERT_TRUE(c.mode == UiMode::Menu);
  TEST_ASSERT_TRUE(parse("M lobby", c));
  TEST_ASSERT_TRUE(c.mode == UiMode::Lobby);
  TEST_ASSERT_TRUE(parse("M rename", c));
  TEST_ASSERT_TRUE(c.mode == UiMode::Rename);
  TEST_ASSERT_FALSE(parse("M fly", c));
}
void test_parse_toggle() {
  WebCmd c;
  TEST_ASSERT_TRUE(parse("P 3", c));
  TEST_ASSERT_TRUE(c.kind == WebCmd::Kind::Toggle);
  TEST_ASSERT_EQUAL_INT(3, c.id);
  TEST_ASSERT_FALSE(parse("P 0", c));
  TEST_ASSERT_FALSE(parse("P 5", c));
}
void test_parse_name() {
  WebCmd c;
  TEST_ASSERT_TRUE(parse("N \xeb\xaf\xbc\xec\x88\x98", c));
  TEST_ASSERT_TRUE(c.kind == WebCmd::Kind::Name);
  TEST_ASSERT_EQUAL_STRING("\xeb\xaf\xbc\xec\x88\x98", c.name);
  TEST_ASSERT_TRUE(parse("N", c));
  TEST_ASSERT_EQUAL_STRING("", c.name);
  TEST_ASSERT_FALSE(parse("N \xeb\xaf\xbc\xec\x88\x98\xec\x95\xbc", c));
  TEST_ASSERT_FALSE(parse("N a\"", c));
}
void test_parse_respects_len() {
  WebCmd c;
  const char buf[] = "J 50 60";  // len 3만 주면 "J 5"
  TEST_ASSERT_FALSE(parseWebCmd(buf, 3, c));
  const char nm[] = "N ab";
  TEST_ASSERT_TRUE(parseWebCmd(nm, 3, c));  // "N a"
  TEST_ASSERT_EQUAL_STRING("a", c.name);
}

void test_state_json_exact() {
  UiSnapshot s = {};
  s.id = 2; s.role = Role::Runner; s.name = "\xeb\xaf\xbc\xec\x88\x98";
  s.mode = UiMode::Lobby; s.phase = Phase::Playing;
  s.spectating = false; s.caughtMe = false; s.lock = false;
  s.participants = 12; s.caughtMask = 8; s.eligible = 0;
  s.msLeft = 157000; s.gameSec = 180; s.winner = Winner::None;
  s.label = DriveLabel::Left; s.battDeciV = 76; s.bs = BatteryState::Ok; s.distCm = -1;
  s.names[0] = ""; s.names[1] = "\xeb\x8c\x80\xec\x9e\xa5"; s.names[2] = "\xeb\xaf\xbc\xec\x88\x98";
  s.names[3] = ""; s.names[4] = "";
  const char* want =
      "{\"id\":2,\"role\":\"runner\",\"name\":\"\xeb\xaf\xbc\xec\x88\x98\",\"mode\":\"lobby\","
      "\"phase\":\"playing\",\"spec\":0,\"me\":0,\"lock\":0,\"part\":12,\"cm\":8,\"elig\":0,"
      "\"ms\":157000,\"sec\":180,\"win\":0,\"label\":\"left\",\"bat\":76,\"bs\":\"ok\",\"dist\":-1,"
      "\"names\":[\"\",\"\xeb\x8c\x80\xec\x9e\xa5\",\"\xeb\xaf\xbc\xec\x88\x98\",\"\",\"\"]}";
  char buf[400];
  size_t n = buildStateJson(s, buf, sizeof buf);
  TEST_ASSERT_EQUAL_UINT(strlen(want), n);
  TEST_ASSERT_EQUAL_STRING(want, buf);
  // 이상한 이름은 빈 문자열로
  s.name = "a\"b";
  n = buildStateJson(s, buf, sizeof buf);
  TEST_ASSERT_NOT_NULL(strstr(buf, "\"name\":\"\","));
}
void test_state_json_too_small() {
  UiSnapshot s = {};
  s.name = ""; for (auto& p : s.names) p = "";
  char buf[20];
  TEST_ASSERT_EQUAL_UINT(0, buildStateJson(s, buf, sizeof buf));
}

void setup() {
  delay(2000);
  UNITY_BEGIN();
  RUN_TEST(test_parse_joy);
  RUN_TEST(test_parse_buttons);
  RUN_TEST(test_parse_mode);
  RUN_TEST(test_parse_toggle);
  RUN_TEST(test_parse_name);
  RUN_TEST(test_parse_respects_len);
  RUN_TEST(test_state_json_exact);
  RUN_TEST(test_state_json_too_small);
  UNITY_END();
}

void loop() {}

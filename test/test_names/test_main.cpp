#include <Arduino.h>
#include <unity.h>
#include "names.h"

using namespace rcar;

void setUp() {}
void tearDown() {}

void test_valid_name_accepts() {
  TEST_ASSERT_TRUE(validName(""));
  TEST_ASSERT_TRUE(validName("민수"));
  TEST_ASSERT_TRUE(validName("A1"));
  TEST_ASSERT_TRUE(validName("김"));
  TEST_ASSERT_TRUE(validName("힣가"));        // U+D7A3, U+AC00 경계
}
void test_valid_name_rejects() {
  TEST_ASSERT_FALSE(validName(nullptr));
  TEST_ASSERT_FALSE(validName("민수야"));     // 3글자
  TEST_ASSERT_FALSE(validName("abc"));
  TEST_ASSERT_FALSE(validName("a\""));        // 따옴표 (JSON 깨짐 방지)
  TEST_ASSERT_FALSE(validName("a b"));
  TEST_ASSERT_FALSE(validName("\xF0\x9F\x98\x80"));  // 이모지
  TEST_ASSERT_FALSE(validName("\xEB\xAF"));   // 잘린 UTF-8
  TEST_ASSERT_FALSE(validName("ㄱ"));         // 자모(음절 아님)
}

void setup() {
  delay(2000);
  UNITY_BEGIN();
  RUN_TEST(test_valid_name_accepts);
  RUN_TEST(test_valid_name_rejects);
  UNITY_END();
}
void loop() {}

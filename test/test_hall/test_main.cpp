#include <Arduino.h>
#include <unity.h>
#include "hall_detector.h"

using namespace rcar;

void setUp() {}
void tearDown() {}

static HallDetector make() {
  HallDetector h;
  h.setBaseline(1650.0f);
  h.setThreshold(30.0f);
  return h;
}

void test_needs_five_consecutive() {
  HallDetector h = make();
  for (int i = 0; i < 4; i++) TEST_ASSERT_FALSE(h.update(1700.0f, true));
  TEST_ASSERT_TRUE(h.update(1700.0f, true));
}
void test_either_pole() {
  HallDetector h = make();
  bool fired = false;
  for (int i = 0; i < 5; i++) fired = h.update(1600.0f, true);  // 아래쪽(반대 극)
  TEST_ASSERT_TRUE(fired);
}
void test_break_resets_count() {
  HallDetector h = make();
  for (int i = 0; i < 4; i++) h.update(1700.0f, true);
  h.update(1655.0f, true);  // 한 번 끊김
  for (int i = 0; i < 4; i++) TEST_ASSERT_FALSE(h.update(1700.0f, true));
  TEST_ASSERT_TRUE(h.update(1700.0f, true));
}
void test_fires_only_once() {
  HallDetector h = make();
  for (int i = 0; i < 5; i++) h.update(1700.0f, true);
  for (int i = 0; i < 20; i++) TEST_ASSERT_FALSE(h.update(1700.0f, true));
}
void test_not_armed_never_fires() {
  HallDetector h = make();
  for (int i = 0; i < 20; i++) TEST_ASSERT_FALSE(h.update(1800.0f, false));
}
void test_drift_only_when_small_and_disarmed() {
  HallDetector h = make();
  for (int i = 0; i < 500; i++) h.update(1655.0f, false);  // 5mV 차이 (기준의 1/4 미만) → 따라감
  TEST_ASSERT_FLOAT_WITHIN(0.5f, 1655.0f, h.baseline());
  HallDetector g = make();
  for (int i = 0; i < 500; i++) g.update(1700.0f, false);  // 큰 차이(자석) → 안 따라감
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 1650.0f, g.baseline());
}
void test_no_drift_while_armed() {
  HallDetector h = make();
  for (int i = 0; i < 500; i++) h.update(1655.0f, true);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 1650.0f, h.baseline());
}

void setup() {
  delay(2000);
  UNITY_BEGIN();
  RUN_TEST(test_needs_five_consecutive);
  RUN_TEST(test_either_pole);
  RUN_TEST(test_break_resets_count);
  RUN_TEST(test_fires_only_once);
  RUN_TEST(test_not_armed_never_fires);
  RUN_TEST(test_drift_only_when_small_and_disarmed);
  RUN_TEST(test_no_drift_while_armed);
  UNITY_END();
}

void loop() {}

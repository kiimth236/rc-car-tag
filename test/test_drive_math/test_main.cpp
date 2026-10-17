#include <Arduino.h>
#include <unity.h>
#include "drive_math.h"

using namespace rcar;

void setUp() {}
void tearDown() {}

void test_max_duty_full_battery() { TEST_ASSERT_EQUAL_INT(219, maxDutyForBattery(8.4f, 5.5f, 2.0f)); }
void test_max_duty_nominal_is_full() { TEST_ASSERT_EQUAL_INT(255, maxDutyForBattery(7.4f, 5.5f, 2.0f)); }
void test_max_duty_no_battery_is_full() { TEST_ASSERT_EQUAL_INT(255, maxDutyForBattery(0.0f, 5.5f, 2.0f)); }

void test_mix_forward() {
  float l, r;
  mixTank(1.0f, 0.0f, l, r);
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 1.0f, l);
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 1.0f, r);
}
void test_mix_pivot_right() {
  float l, r;
  mixTank(0.0f, 1.0f, l, r);
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 1.0f, l);
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, -1.0f, r);
}
void test_mix_clamps() {
  float l, r;
  mixTank(1.0f, 0.6f, l, r);
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 1.0f, l);
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.4f, r);
}

void test_duty_zero_deadband() { TEST_ASSERT_EQUAL_INT(0, commandToDuty(0.01f, 90, 219)); }
void test_duty_full() { TEST_ASSERT_EQUAL_INT(219, commandToDuty(1.0f, 90, 219)); }
void test_duty_reverse_half() { TEST_ASSERT_EQUAL_INT(-155, commandToDuty(-0.5f, 90, 219)); }
// 0.03 → 90 + lround(0.03 × 129 = 3.87) = 94 : 작은 명령도 최소 듀티부터 시작
void test_duty_small_starts_at_min() { TEST_ASSERT_EQUAL_INT(94, commandToDuty(0.03f, 90, 219)); }
void test_duty_min_above_max_uses_max() { TEST_ASSERT_EQUAL_INT(100, commandToDuty(0.1f, 150, 100)); }

void test_ramp_limits_step() { TEST_ASSERT_EQUAL_INT(13, rampStep(0, 200, 13)); }
void test_ramp_reaches_target() { TEST_ASSERT_EQUAL_INT(200, rampStep(195, 200, 13)); }
void test_ramp_reversal_passes_zero() {
  int d = 219;
  bool sawZero = false;
  for (int i = 0; i < 60; i++) {
    int next = rampStep(d, -219, 13);
    TEST_ASSERT_TRUE(abs(next - d) <= 13);
    TEST_ASSERT_FALSE(d > 0 && next < 0);  // 0을 건너뛰고 부호가 바뀌면 안 됨
    if (next == 0) sawZero = true;
    d = next;
  }
  TEST_ASSERT_TRUE(sawZero);
  TEST_ASSERT_EQUAL_INT(-219, d);
}

void test_forward_limit_far() { TEST_ASSERT_FLOAT_WITHIN(1e-4f, 1.0f, limitForward(1.0f, 50.0f, 10.0f, 3.0f, 0.4f)); }
void test_forward_limit_slow() { TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.4f, limitForward(1.0f, 8.0f, 10.0f, 3.0f, 0.4f)); }
void test_forward_limit_stop() { TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.0f, limitForward(1.0f, 2.5f, 10.0f, 3.0f, 0.4f)); }
void test_forward_limit_allows_reverse() { TEST_ASSERT_FLOAT_WITHIN(1e-4f, -1.0f, limitForward(-1.0f, 2.0f, 10.0f, 3.0f, 0.4f)); }
void test_forward_limit_no_echo() { TEST_ASSERT_FLOAT_WITHIN(1e-4f, 1.0f, limitForward(1.0f, -1.0f, 10.0f, 3.0f, 0.4f)); }

void test_median3_normal() { TEST_ASSERT_FLOAT_WITHIN(1e-4f, 20.0f, median3Distance(30.0f, 10.0f, 20.0f)); }
void test_median3_one_missing() { TEST_ASSERT_FLOAT_WITHIN(1e-4f, 20.0f, median3Distance(-1.0f, 10.0f, 20.0f)); }
void test_median3_two_missing() { TEST_ASSERT_FLOAT_WITHIN(1e-4f, -1.0f, median3Distance(-1.0f, 10.0f, -1.0f)); }

void setup() {
  delay(2000);
  UNITY_BEGIN();
  RUN_TEST(test_max_duty_full_battery);
  RUN_TEST(test_max_duty_nominal_is_full);
  RUN_TEST(test_max_duty_no_battery_is_full);
  RUN_TEST(test_mix_forward);
  RUN_TEST(test_mix_pivot_right);
  RUN_TEST(test_mix_clamps);
  RUN_TEST(test_duty_zero_deadband);
  RUN_TEST(test_duty_full);
  RUN_TEST(test_duty_reverse_half);
  RUN_TEST(test_duty_small_starts_at_min);
  RUN_TEST(test_duty_min_above_max_uses_max);
  RUN_TEST(test_ramp_limits_step);
  RUN_TEST(test_ramp_reaches_target);
  RUN_TEST(test_ramp_reversal_passes_zero);
  RUN_TEST(test_forward_limit_far);
  RUN_TEST(test_forward_limit_slow);
  RUN_TEST(test_forward_limit_stop);
  RUN_TEST(test_forward_limit_allows_reverse);
  RUN_TEST(test_forward_limit_no_echo);
  RUN_TEST(test_median3_normal);
  RUN_TEST(test_median3_one_missing);
  RUN_TEST(test_median3_two_missing);
  UNITY_END();
}

void loop() {}

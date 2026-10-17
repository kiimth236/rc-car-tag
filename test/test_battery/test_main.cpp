#include <Arduino.h>
#include <unity.h>
#include "battery_monitor.h"

using namespace rcar;

// alpha = 1 → 이동평균 없이 바로 반영 (판정 규칙만 시험)
static const BatteryConfig kInstant{6.8f, 7.0f, 6.4f, 3.0f, 3000, 1.0f};

void setUp() {}
void tearDown() {}

void test_first_sample_sets_filtered() {
  BatteryMonitor b;
  b.update(7.4f, 0);
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 7.4f, b.filtered());
  TEST_ASSERT_EQUAL(BatteryState::Ok, b.state());
}
void test_warn_and_hysteresis() {
  BatteryMonitor b(kInstant);
  b.update(6.7f, 0);
  TEST_ASSERT_EQUAL(BatteryState::Warn, b.state());
  b.update(6.9f, 100);  // 7.0 미만이면 경고 유지
  TEST_ASSERT_EQUAL(BatteryState::Warn, b.state());
  b.update(7.1f, 200);
  TEST_ASSERT_EQUAL(BatteryState::Ok, b.state());
}
void test_cutoff_after_hold_and_latches() {
  BatteryMonitor b(kInstant);
  b.update(6.3f, 0);
  TEST_ASSERT_EQUAL(BatteryState::Warn, b.state());
  b.update(6.3f, 2999);
  TEST_ASSERT_EQUAL(BatteryState::Warn, b.state());
  b.update(6.3f, 3000);
  TEST_ASSERT_EQUAL(BatteryState::Cutoff, b.state());
  b.update(8.0f, 4000);  // 올라가도 풀리지 않음
  TEST_ASSERT_EQUAL(BatteryState::Cutoff, b.state());
}
void test_short_dip_does_not_latch() {
  BatteryMonitor b(kInstant);
  b.update(6.3f, 0);
  b.update(6.3f, 2900);
  b.update(7.2f, 3000);
  b.update(6.3f, 3100);
  b.update(6.3f, 5000);  // 다시 내려간 지 1.9초
  TEST_ASSERT_EQUAL(BatteryState::Warn, b.state());
}
void test_zero_volts_is_no_battery() {
  BatteryMonitor b;
  b.update(0.05f, 0);
  TEST_ASSERT_EQUAL(BatteryState::NoBattery, b.state());
}
void test_switch_off_never_latches() {
  BatteryMonitor b;  // 기본 alpha 0.05, 100ms마다
  uint32_t t = 0;
  for (int i = 0; i < 50; i++, t += 100) b.update(7.4f, t);
  for (int i = 0; i < 200; i++, t += 100) {  // 스위치 OFF → USB만 (0V)
    b.update(0.0f, t);
    TEST_ASSERT_NOT_EQUAL(BatteryState::Cutoff, b.state());
  }
  TEST_ASSERT_EQUAL(BatteryState::NoBattery, b.state());
}

void test_switch_on_from_usb_never_latches() {
  BatteryMonitor b;  // USB 만 연결된 상태에서 배터리 스위치 ON (7.0V)
  uint32_t t = 0;
  for (int i = 0; i < 200; i++, t += 100) b.update(0.0f, t);
  for (int i = 0; i < 100; i++, t += 100) {
    b.update(7.0f, t);
    TEST_ASSERT_NOT_EQUAL(BatteryState::Cutoff, b.state());
  }
  TEST_ASSERT_EQUAL(BatteryState::Ok, b.state());
}
void test_switch_off_clears_latch() {
  BatteryMonitor b(kInstant);
  b.update(6.3f, 0);
  b.update(6.3f, 3000);
  TEST_ASSERT_EQUAL(BatteryState::Cutoff, b.state());
  b.update(0.0f, 3100);  // 스위치 OFF (USB 로 ESP32 는 계속 켜져 있음)
  TEST_ASSERT_EQUAL(BatteryState::NoBattery, b.state());
  b.update(7.6f, 3200);  // 충전한 배터리로 다시 ON
  TEST_ASSERT_EQUAL(BatteryState::Ok, b.state());
}

void setup() {
  delay(2000);
  UNITY_BEGIN();
  RUN_TEST(test_switch_on_from_usb_never_latches);
  RUN_TEST(test_switch_off_clears_latch);
  RUN_TEST(test_first_sample_sets_filtered);
  RUN_TEST(test_warn_and_hysteresis);
  RUN_TEST(test_cutoff_after_hold_and_latches);
  RUN_TEST(test_short_dip_does_not_latch);
  RUN_TEST(test_zero_volts_is_no_battery);
  RUN_TEST(test_switch_off_never_latches);
  UNITY_END();
}

void loop() {}

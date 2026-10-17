#include <Arduino.h>
#include <unity.h>
#include "led_patterns.h"

using namespace rcar;

void setUp() {}
void tearDown() {}

static void assertRgb(Rgb want, Rgb got) {
  TEST_ASSERT_EQUAL_UINT8(want.r, got.r);
  TEST_ASSERT_EQUAL_UINT8(want.g, got.g);
  TEST_ASSERT_EQUAL_UINT8(want.b, got.b);
}

void test_role_colors() {
  assertRgb({80, 80, 80}, roleColor(Role::Chaser, 3, 80));
  assertRgb({0, 80, 0}, roleColor(Role::Runner, 1, 80));
  assertRgb({0, 0, 80}, roleColor(Role::Runner, 2, 80));
  assertRgb({80, 80, 0}, roleColor(Role::Runner, 3, 80));
  assertRgb({0, 80, 80}, roleColor(Role::Runner, 4, 80));
}
void test_solid_and_caught() {
  assertRgb({0, 0, 80}, patternColor(LedPattern::RoleSolid, Role::Runner, 2, 0, BatteryState::Ok, 123, 80));
  assertRgb({80, 0, 0}, patternColor(LedPattern::Caught, Role::Runner, 2, 0, BatteryState::Ok, 123, 80));
}
void test_idle_blinks_1hz() {
  assertRgb({0, 80, 0}, patternColor(LedPattern::IdleBlink, Role::Runner, 1, 0, BatteryState::Ok, 100, 80));
  assertRgb({0, 0, 0}, patternColor(LedPattern::IdleBlink, Role::Runner, 1, 0, BatteryState::Ok, 600, 80));
}
void test_countdown_flash_each_second() {
  assertRgb({0, 80, 0}, patternColor(LedPattern::Countdown, Role::Runner, 1, 2900, BatteryState::Ok, 0, 80));
  assertRgb({0, 0, 0}, patternColor(LedPattern::Countdown, Role::Runner, 1, 2500, BatteryState::Ok, 0, 80));
}
void test_cutoff_overrides_red_blink() {
  assertRgb({80, 0, 0}, patternColor(LedPattern::RoleSolid, Role::Runner, 1, 0, BatteryState::Cutoff, 100, 80));
  assertRgb({0, 0, 0}, patternColor(LedPattern::RoleSolid, Role::Runner, 1, 0, BatteryState::Cutoff, 300, 80));
}
void test_warn_yellow_flash() {
  assertRgb({80, 80, 0}, patternColor(LedPattern::RoleSolid, Role::Runner, 2, 0, BatteryState::Warn, 2100, 80));
  assertRgb({0, 0, 80}, patternColor(LedPattern::RoleSolid, Role::Runner, 2, 0, BatteryState::Warn, 2500, 80));
}

void setup() {
  delay(2000);
  UNITY_BEGIN();
  RUN_TEST(test_role_colors);
  RUN_TEST(test_solid_and_caught);
  RUN_TEST(test_idle_blinks_1hz);
  RUN_TEST(test_countdown_flash_each_second);
  RUN_TEST(test_cutoff_overrides_red_blink);
  RUN_TEST(test_warn_yellow_flash);
  UNITY_END();
}

void loop() {}

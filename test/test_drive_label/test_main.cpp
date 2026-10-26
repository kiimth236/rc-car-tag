#include <Arduino.h>
#include <unity.h>
#include "drive_label.h"

using namespace rcar;

void setUp() {}
void tearDown() {}

void test_stop() {
  TEST_ASSERT_EQUAL(DriveLabel::Stop, classifyDrive(0, 0, false));
  TEST_ASSERT_EQUAL(DriveLabel::Stop, classifyDrive(4, -3, false));
}
void test_forward_back() {
  TEST_ASSERT_EQUAL(DriveLabel::Forward, classifyDrive(200, 180, false));
  TEST_ASSERT_EQUAL(DriveLabel::Back, classifyDrive(-200, -170, false));
}
void test_turns() {
  TEST_ASSERT_EQUAL(DriveLabel::Left, classifyDrive(100, 200, false));
  TEST_ASSERT_EQUAL(DriveLabel::Right, classifyDrive(200, 90, false));
  TEST_ASSERT_EQUAL(DriveLabel::Left, classifyDrive(0, 150, false));
  TEST_ASSERT_EQUAL(DriveLabel::Right, classifyDrive(-150, -255, false));
  TEST_ASSERT_EQUAL(DriveLabel::Left, classifyDrive(-200, -60, false));
}
void test_spin() {
  TEST_ASSERT_EQUAL(DriveLabel::SpinLeft, classifyDrive(-150, 150, false));
  TEST_ASSERT_EQUAL(DriveLabel::SpinRight, classifyDrive(150, -150, false));
}
void test_blocked_wins() {
  TEST_ASSERT_EQUAL(DriveLabel::Blocked, classifyDrive(0, 0, true));
}

void setup() {
  delay(2000);
  UNITY_BEGIN();
  RUN_TEST(test_stop);
  RUN_TEST(test_forward_back);
  RUN_TEST(test_turns);
  RUN_TEST(test_spin);
  RUN_TEST(test_blocked_wins);
  UNITY_END();
}
void loop() {}

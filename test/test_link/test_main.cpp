#include <Arduino.h>
#include <unity.h>
#include "link_watch.h"

using namespace rcar;

void setUp() {}
void tearDown() {}

void test_not_linked_is_not_connected() {
  LinkWatch w(500);
  w.onRx(0);
  TEST_ASSERT_FALSE(w.update(false, 10));
}
void test_linked_without_data_is_not_connected() {
  LinkWatch w(500);
  TEST_ASSERT_FALSE(w.update(true, 10));
}
void test_rx_timeout_stops() {
  LinkWatch w(500);
  w.update(true, 0);
  w.onRx(100);
  TEST_ASSERT_TRUE(w.update(true, 599));
  TEST_ASSERT_FALSE(w.update(true, 600));  // 0.5초 동안 새 명령 없음
}
void test_zero_timeout_keeps_link_only() {
  LinkWatch w(0);
  w.update(true, 0);
  w.onRx(100);
  TEST_ASSERT_TRUE(w.update(true, 100000));
  TEST_ASSERT_FALSE(w.update(false, 100010));
}
void test_reconnect_needs_new_data() {
  LinkWatch w(0);
  w.update(true, 0);
  w.onRx(10);
  TEST_ASSERT_TRUE(w.update(true, 20));
  w.update(false, 30);
  TEST_ASSERT_FALSE(w.update(true, 40));  // 다시 붙었지만 아직 새 입력 없음 (옛 조이스틱 값 무시)
  w.onRx(50);
  TEST_ASSERT_TRUE(w.update(true, 60));
}
void test_readvertise_once_on_disconnect() {
  LinkWatch w(500);
  w.update(true, 0);
  TEST_ASSERT_FALSE(w.takeReadvertise());
  w.update(false, 10);
  TEST_ASSERT_TRUE(w.takeReadvertise());
  w.update(false, 20);
  TEST_ASSERT_FALSE(w.takeReadvertise());
}

void setup() {
  delay(2000);
  UNITY_BEGIN();
  RUN_TEST(test_not_linked_is_not_connected);
  RUN_TEST(test_linked_without_data_is_not_connected);
  RUN_TEST(test_rx_timeout_stops);
  RUN_TEST(test_zero_timeout_keeps_link_only);
  RUN_TEST(test_reconnect_needs_new_data);
  RUN_TEST(test_readvertise_once_on_disconnect);
  UNITY_END();
}

void loop() {}

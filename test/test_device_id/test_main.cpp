#include <unity.h>

#include "device_id.h"

const uint8_t kMac[6] = {0x24, 0x6F, 0x28, 0xA1, 0xB2, 0xC3};

void setUp() {}
void tearDown() {}

void test_device_id_uses_last_three_bytes_lowercase() {
  TEST_ASSERT_EQUAL_STRING("homeguard-a1b2c3", deviceIdFromMac(kMac).c_str());
}

void test_ble_name_uses_last_three_bytes_uppercase() {
  TEST_ASSERT_EQUAL_STRING("HomeGuard-A1B2C3", bleNameFromMac(kMac).c_str());
}

void test_leading_zero_bytes_are_padded() {
  const uint8_t mac[6] = {0, 0, 0, 0x01, 0x0A, 0x00};
  TEST_ASSERT_EQUAL_STRING("homeguard-010a00", deviceIdFromMac(mac).c_str());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_device_id_uses_last_three_bytes_lowercase);
  RUN_TEST(test_ble_name_uses_last_three_bytes_uppercase);
  RUN_TEST(test_leading_zero_bytes_are_padded);
  return UNITY_END();
}

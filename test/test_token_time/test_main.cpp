#include <unity.h>

#include "token_time.h"

static const char* kToken =
    "eyJhbGciOiJSUzI1NiIsInR5cCI6IkpXVCJ9."
    "eyJpc3MiOiJodHRwczovL3NlY3VyZXRva2VuLmdvb2dsZS5jb20vcCIsImlhdCI6MTc4OTI2MjUwMCwiZXhwIjoxNzg5MjY2MTAwLCJ1c2VyX2lkIjoidT9-In0."
    "sig";

void setUp() {}
void tearDown() {}

void test_reads_issued_at_from_token() {
  std::optional<int64_t> iat = tokenIssuedAtSec(kToken);
  TEST_ASSERT_TRUE(iat.has_value());
  TEST_ASSERT_TRUE(*iat == 1789262500LL);
}

void test_token_without_issued_at() { TEST_ASSERT_FALSE(tokenIssuedAtSec("e30.eyJleHAiOjF9.sig").has_value()); }

void test_malformed_tokens() {
  TEST_ASSERT_FALSE(tokenIssuedAtSec("").has_value());
  TEST_ASSERT_FALSE(tokenIssuedAtSec("no-dots").has_value());
  TEST_ASSERT_FALSE(tokenIssuedAtSec("a.!!!.b").has_value());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_reads_issued_at_from_token);
  RUN_TEST(test_token_without_issued_at);
  RUN_TEST(test_malformed_tokens);
  return UNITY_END();
}

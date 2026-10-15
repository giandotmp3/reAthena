include(GoogleTest)

add_executable(rathena_login_tests
  auth_order_test.cpp
  registration_test.cpp
  ip_ban_test.cpp
  session_test.cpp
  char_server_test.cpp
  account_admin_test.cpp
  web_token_test.cpp
  password_pool_test.cpp
  protocol_test.cpp
  rule_manifest_test.cpp
)
target_link_libraries(rathena_login_tests PRIVATE rathena::login GTest::gtest_main)

# Each test name embeds its BR-ID ('-' -> '_'); pending rules use GTEST_SKIP
# with the rule ID in the message, so ctest reports them as skipped.
gtest_discover_tests(rathena_login_tests DISCOVERY_MODE PRE_TEST PROPERTIES TIMEOUT 60)

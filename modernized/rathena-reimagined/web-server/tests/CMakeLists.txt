# BR-WEB acceptance tests. Test suite names carry the rule ID (BR_WEB_NNN_*);
# pending rules use GTEST_SKIP with the rule ID in the message.
add_executable(web_acceptance_tests
  test_support.cpp
  test_auth.cpp
  test_emblem.cpp
  test_config.cpp
  test_party_booking.cpp
  test_cors.cpp
  test_rule_coverage.cpp
  test_config_env.cpp
)
target_link_libraries(web_acceptance_tests PRIVATE rathena::web GTest::gtest_main)
target_include_directories(web_acceptance_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

include(GoogleTest)
gtest_discover_tests(web_acceptance_tests DISCOVERY_MODE PRE_TEST)

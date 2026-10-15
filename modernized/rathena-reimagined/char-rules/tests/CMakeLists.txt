include(GoogleTest)

add_executable(rathena_char_rules_tests
  test_charlife_create.cpp
  test_charlife_delete.cpp
  test_charlife_misc.cpp
  test_pincode.cpp
  test_session.cpp
  test_party.cpp
  test_guild.cpp
  test_mail.cpp
  test_registry.cpp
  test_clan.cpp
  test_gm.cpp
  test_save_auth_barrier.cpp
  test_rule_manifest.cpp
)
target_link_libraries(rathena_char_rules_tests PRIVATE rathena::char_rules GTest::gtest_main)

# Each test name embeds its BR-ID ('-' -> '_'); pending rules use
# GTEST_SKIP() << "PENDING BR-XXX-NNN: ..." so ctest reports them as skipped, not passed.
gtest_discover_tests(rathena_char_rules_tests DISCOVERY_MODE PRE_TEST PROPERTIES TIMEOUT 60)

# Architecture §8: CI fails on any rule without a test (passing or pending).
add_test(NAME char_rules_rule_coverage
  COMMAND ${CMAKE_COMMAND}
    -DMANIFEST=${PROJECT_SOURCE_DIR}/include/rathena/char_rules/rule_manifest.hpp
    -DTEST_DIR=${CMAKE_CURRENT_SOURCE_DIR}
    -P ${CMAKE_CURRENT_SOURCE_DIR}/check_rule_coverage.cmake)

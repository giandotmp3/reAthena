include(GoogleTest)

add_executable(rathena_core_tests
  test_secrets.cpp
  test_logging_redaction.cpp
  test_battle_config.cpp
  test_config_loader.cpp
  test_rng.cpp
  test_sha256.cpp
)
target_link_libraries(rathena_core_tests PRIVATE rathena::core GTest::gtest_main)
target_compile_definitions(rathena_core_tests PRIVATE
  RATHENA_CORE_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
  RATHENA_CORE_CONF_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../conf")

# Each test name embeds its BR-ID / spec section; ctest lists them individually.
gtest_discover_tests(rathena_core_tests DISCOVERY_MODE PRE_TEST PROPERTIES TIMEOUT 60)

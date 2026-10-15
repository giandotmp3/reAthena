include(GoogleTest)

add_executable(rathena_core_tests
  test_secrets.cpp
  test_logging_redaction.cpp
  test_battle_config.cpp
  test_config_loader.cpp
  test_rng.cpp
  test_sha256.cpp
  test_shipped_conf_parity.cpp
)
target_link_libraries(rathena_core_tests PRIVATE rathena::core GTest::gtest_main)
target_compile_definitions(rathena_core_tests PRIVATE
  RATHENA_CORE_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
  # Golden vectors captured from legacy code by tools/golden/capture.sh
  RATHENA_CORE_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/golden"
  RATHENA_CORE_CONF_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../conf"
  # Legacy conf/ tree at the repository root (drift guard, spec §5.6)
  RATHENA_LEGACY_CONF_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../../../conf")

# Each test name embeds its BR-ID / spec section; ctest lists them individually.
gtest_discover_tests(rathena_core_tests DISCOVERY_MODE PRE_TEST PROPERTIES TIMEOUT 60)

# The generated battle knob table must match legacy battle_data[] and conf/battle (spec §5.6).
# Runs only inside the repository (the generator reads src/ and conf/) and with Python 3.10+.
set(RATHENA_BATTLE_KNOBS_GENERATOR "${CMAKE_CURRENT_SOURCE_DIR}/../../../../tools/codegen/battle_knobs.py")
if(EXISTS "${RATHENA_BATTLE_KNOBS_GENERATOR}")
  find_package(Python3 3.10 COMPONENTS Interpreter)
  if(Python3_Interpreter_FOUND)
    add_test(NAME SPEC_5_6_GeneratedBattleKnobsUpToDate
             COMMAND Python3::Interpreter "${RATHENA_BATTLE_KNOBS_GENERATOR}" --check)
    set_tests_properties(SPEC_5_6_GeneratedBattleKnobsUpToDate PROPERTIES TIMEOUT 60)
  endif()
endif()

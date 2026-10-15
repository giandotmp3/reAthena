# One file per rule group; every acceptance test name carries its BR-ID
# (BR-MAP-014 -> BR_MAP_014_..., since gtest names cannot contain '-').
# Pending rules call GTEST_SKIP() with "PENDING BR-MAP-NNN" and still count toward coverage.
add_executable(map_rules_acceptance_tests
  combat_test.cpp
  progression_test.cpp
  economy_test.cpp
  trade_test.cpp
  skills_test.cpp
  anticheat_test.cpp
  property_test.cpp
  rule_manifest_test.cpp
  map_module_manifest_test.cpp
)
target_link_libraries(map_rules_acceptance_tests PRIVATE rathena::map_rules GTest::gtest_main)

# Manifest locations for the checker tests. The legacy directory is only *read* (never written)
# to verify that every src/map/*.cpp has an owner; the test skips if it is absent.
set(RATHENA_LEGACY_MAP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../../src/map"
    CACHE PATH "Legacy src/map directory scanned (read-only) by the module-manifest checker")
target_compile_definitions(map_rules_acceptance_tests PRIVATE
  MAP_RULES_MANIFEST_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../manifest"
  MAP_RULES_TESTS_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
  RATHENA_LEGACY_MAP_DIR="${RATHENA_LEGACY_MAP_DIR}")

if(MSVC)
  target_compile_options(map_rules_acceptance_tests PRIVATE /W4 /permissive-)
else()
  target_compile_options(map_rules_acceptance_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

include(GoogleTest)
gtest_discover_tests(map_rules_acceptance_tests)

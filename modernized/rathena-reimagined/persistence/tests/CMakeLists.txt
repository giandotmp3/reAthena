include(GoogleTest)

add_executable(rathena_persistence_tests
  zeny_test.cpp
  item_mapping_test.cpp
  account_repository_test.cpp
  character_delete_test.cpp
  repositories_test.cpp
  db_config_test.cpp
  table_names_test.cpp
  rule_manifest_test.cpp
  sqlite_repository_test.cpp
  repository_contract_test.cpp
  postgres_test.cpp
  import_test.cpp
)
target_link_libraries(rathena_persistence_tests PRIVATE rathena::persistence GTest::gtest_main)
# Generated schema files (read-only) for the importer tests in import_test.cpp
target_compile_definitions(rathena_persistence_tests PRIVATE
  RATHENA_LEGACY_SQL_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../../../sql-files")

# Each test name embeds its BR-ID (or step id); ctest lists them individually. Tests that need
# RATHENA_TEST_PG_URL skip without it; with it, each test process works in its own schema, so
# `ctest -j` is safe.
# Pending rules call GTEST_SKIP() with "PENDING <BR-ID>" and show up as skipped.
gtest_discover_tests(rathena_persistence_tests DISCOVERY_MODE PRE_TEST)

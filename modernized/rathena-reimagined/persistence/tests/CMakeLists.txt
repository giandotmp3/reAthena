include(GoogleTest)

add_executable(rathena_persistence_tests
  zeny_test.cpp
  item_mapping_test.cpp
  account_repository_test.cpp
  character_delete_test.cpp
  repositories_test.cpp
  db_config_test.cpp
  table_names_test.cpp
  migration_test.cpp
  rule_manifest_test.cpp
  mysql_integration_test.cpp
  mysql_migration_integration_test.cpp
  sqlite_repository_test.cpp
)
target_link_libraries(rathena_persistence_tests PRIVATE rathena::persistence GTest::gtest_main)
# Legacy DDL (read-only) for the live-database tests in mysql_integration_test.cpp
target_compile_definitions(rathena_persistence_tests PRIVATE
  RATHENA_LEGACY_SQL_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../../../sql-files")

# Each test name embeds its BR-ID (or migration step id); ctest lists them individually.
# Pending rules call GTEST_SKIP() with "PENDING <BR-ID>" and show up as skipped.
gtest_discover_tests(rathena_persistence_tests DISCOVERY_MODE PRE_TEST)

set(PERSIST_TEST_SOURCES
  config_test.cpp
  domain_test.cpp
  save_admission_test.cpp
  registry_scope_test.cpp
  write_behind_test.cpp
  wal_test.cpp
  migration_test.cpp
  id_map_test.cpp
  quarantine_quota_test.cpp
  backend_contract_test.cpp
  sqlite_backend_test.cpp
  kv_store_test.cpp
  wal_recovery_test.cpp
  write_behind_service_test.cpp
  failure_injection_test.cpp
  mass_save_test.cpp)
if(RATHENA_PERSIST_HAVE_SCRIPT_HOST)
  list(APPEND PERSIST_TEST_SOURCES script_host_adapter_test.cpp)
endif()
add_executable(persist_tests ${PERSIST_TEST_SOURCES})
target_link_libraries(persist_tests PRIVATE rathena::persist)
if(RATHENA_PERSIST_HAVE_SCRIPT_HOST)
  target_link_libraries(persist_tests PRIVATE rathena::persist_scripthost)
endif()
target_compile_options(persist_tests PRIVATE ${_warn})
if(NOT RATHENA_PERSIST_SANITIZE STREQUAL "none")
  target_compile_definitions(persist_tests PRIVATE RATHENA_PERSIST_SANITIZED=1)
endif()
if(TARGET GTest::gtest_main)
  target_link_libraries(persist_tests PRIVATE GTest::gtest_main)
else()
  target_link_libraries(persist_tests PRIVATE gtest_main)
endif()
# Discover by test name so `ctest -R BR_CHAR_032` selects a rule's tests.
gtest_discover_tests(persist_tests DISCOVERY_MODE PRE_TEST)

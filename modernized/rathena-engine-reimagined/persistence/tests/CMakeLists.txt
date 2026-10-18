add_executable(persist_tests
  config_test.cpp
  domain_test.cpp
  save_admission_test.cpp
  registry_scope_test.cpp
  write_behind_test.cpp
  wal_test.cpp
  migration_test.cpp
  id_map_test.cpp
  quarantine_quota_test.cpp
  backend_contract_test.cpp)
target_link_libraries(persist_tests PRIVATE rathena::persist)
if(TARGET GTest::gtest_main)
  target_link_libraries(persist_tests PRIVATE GTest::gtest_main)
else()
  target_link_libraries(persist_tests PRIVATE gtest_main)
endif()
# Discover by test name so `ctest -R BR_CHAR_032` selects a rule's tests.
gtest_discover_tests(persist_tests DISCOVERY_MODE PRE_TEST)

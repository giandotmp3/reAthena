add_executable(script_host_tests
  main.cpp test_domain.cpp test_numeric.cpp test_event_bus.cpp
  test_budget_sandbox.cpp test_dialogue.cpp test_reload_storage_schema.cpp)
target_link_libraries(script_host_tests PRIVATE script_host)
target_compile_definitions(script_host_tests PRIVATE
  PROBE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../luau/sandbox_probes"
  API_SCHEMA_PATH="${CMAKE_CURRENT_SOURCE_DIR}/../api/engine.yaml")
add_test(NAME script_host_tests COMMAND script_host_tests)

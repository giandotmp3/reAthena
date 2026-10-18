add_executable(script_host_tests
  main.cpp test_domain.cpp test_numeric.cpp test_event_bus.cpp
  test_budget_sandbox.cpp test_dialogue.cpp test_reload_storage_schema.cpp
  test_integration.cpp test_yaml_api.cpp)
target_link_libraries(script_host_tests PRIVATE script_host)
if(NOT MSVC)
  target_compile_options(script_host_tests PRIVATE -Wall -Wextra -Werror)
endif()
target_compile_definitions(script_host_tests PRIVATE
  PROBE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../luau/sandbox_probes"
  API_SCHEMA_PATH="${CMAKE_CURRENT_SOURCE_DIR}/../api/engine.yaml"
  API_DECL_PATH="${CMAKE_CURRENT_SOURCE_DIR}/../api/engine.d.luau")
add_test(NAME script_host_tests COMMAND script_host_tests)

# Declaration check through Luau::Frontend (needs the Luau analysis libraries).
if(SCRIPT_HOST_LUAU_ANALYSIS_FOUND)
  add_executable(decl_check decl_check.cpp)
  target_include_directories(decl_check SYSTEM PRIVATE
    "${LUAU_ROOT}/Analysis/include" "${LUAU_ROOT}/Ast/include" "${LUAU_ROOT}/Config/include"
    "${LUAU_ROOT}/Common/include" "${LUAU_ROOT}/EqSat/include" "${LUAU_ROOT}/Compiler/include" "${LUAU_ROOT}/VM/include")
  target_link_libraries(decl_check PRIVATE "${LUAU_ANALYSIS_LIB}" "${LUAU_CONFIG_LIB}" "${LUAU_AST_LIB}" "${LUAU_EQSAT_LIB}" "${LUAU_COMPILER_LIB}" "${LUAU_VM_LIB}")
  target_compile_definitions(decl_check PRIVATE
    API_DECL_PATH="${CMAKE_CURRENT_SOURCE_DIR}/../api/engine.d.luau"
    MOD_SNIPPET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../luau/typecheck")
  add_test(NAME engine_d_luau_typecheck COMMAND decl_check)
  target_compile_definitions(script_host_tests PRIVATE HAVE_DECL_CHECK=1 DECL_CHECK_EXE="$<TARGET_FILE:decl_check>")
  add_dependencies(script_host_tests decl_check)
endif()

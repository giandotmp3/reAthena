cmake_minimum_required(VERSION 3.20)
project(legacy_npc VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# legacy-npc = C++ facade (declaration/command/event model, DB-script compiler, report) + the Luau package in luau/ (parser, lowering,
# interpreter, commands, runtime) that runs inside a script-host sandbox. The Luau VM comes from script-host: set RATHENA_LUAU_ROOT
# (headers) and RATHENA_LUAU_LIB_DIR (libLuau.*.a) as in script-host/README.md. Without Luau everything that needs the VM is SKIPPED.
option(LEGACY_NPC_BUILD_TESTS "Build acceptance tests" ON)
set(LEGACY_NPC_SCRIPT_HOST_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../script-host" CACHE PATH "script-host unit")

set(SCRIPT_HOST_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory("${LEGACY_NPC_SCRIPT_HOST_DIR}" script-host EXCLUDE_FROM_ALL)

add_library(legacy_npc
  src/variables.cpp
  src/commands.cpp
  src/events.cpp
  src/parser.cpp
  src/interpreter.cpp
  src/db_script.cpp
  src/report.cpp
  src/package.cpp)
target_include_directories(legacy_npc PUBLIC include)
target_compile_features(legacy_npc PUBLIC cxx_std_20)
target_link_libraries(legacy_npc PUBLIC script_host)
target_compile_definitions(legacy_npc PUBLIC LEGACY_NPC_LUAU_DIR="${CMAKE_CURRENT_SOURCE_DIR}/luau")
if(MSVC)
  target_compile_options(legacy_npc PRIVATE /W4 /WX)
else()
  target_compile_options(legacy_npc PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

if(LEGACY_NPC_BUILD_TESTS)
  enable_testing()
  add_executable(legacy_npc_tests tests/main.cpp tests/test_variables.cpp
    tests/test_commands_safety.cpp tests/test_events.cpp tests/test_parser.cpp
    tests/test_interpreter.cpp tests/test_db_script.cpp tests/test_report.cpp
    tests/test_luau_suites.cpp tests/test_integration.cpp)
  target_link_libraries(legacy_npc_tests PRIVATE legacy_npc)
  if(NOT MSVC)
    target_compile_options(legacy_npc_tests PRIVATE -Wall -Wextra -Werror)
  endif()
  add_test(NAME legacy_npc_acceptance COMMAND legacy_npc_tests)

  # Replay regression and corpus memory test: Python drivers around external artifacts. They SKIP (exit 77) unless pointed at them:
  #   LNPC_REPLAY_BUILD / LNPC_REPLAY_BUILD_PRE (tooling/m0/replay/build_legacy.sh outputs) and LNPC_LUAU_CLI for the replay;
  #   LNPC_LUAU_CLI and the repository root (this checkout) for the memory test.
  find_package(Python3 COMPONENTS Interpreter)
  if(Python3_FOUND)
    add_test(NAME legacy_npc_replay_regression
      COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/tests/replay_regression.py --from-env)
    add_test(NAME legacy_npc_memory
      COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/tests/memory_test.py --from-env)
    set_tests_properties(legacy_npc_replay_regression legacy_npc_memory PROPERTIES SKIP_RETURN_CODE 77)
  endif()
endif()

cmake_minimum_required(VERSION 3.20)
project(legacy_npc VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# The Luau-hosted interpreter (luau/interpreter.luau) is loaded by script-host.
# The scaffold builds without Luau so the domain/contract tests run anywhere.
option(LEGACY_NPC_BUILD_TESTS "Build acceptance tests" ON)

add_library(legacy_npc
  src/variables.cpp
  src/commands.cpp
  src/events.cpp
  src/parser.cpp
  src/interpreter.cpp
  src/db_script.cpp
  src/report.cpp)
target_include_directories(legacy_npc PUBLIC include)
target_compile_features(legacy_npc PUBLIC cxx_std_20)
if(MSVC)
  target_compile_options(legacy_npc PRIVATE /W4 /WX)
else()
  target_compile_options(legacy_npc PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

if(LEGACY_NPC_BUILD_TESTS)
  enable_testing()
  add_executable(legacy_npc_tests tests/main.cpp tests/test_variables.cpp
    tests/test_commands_safety.cpp tests/test_events.cpp tests/test_parser.cpp
    tests/test_interpreter.cpp tests/test_db_script.cpp tests/test_report.cpp)
  target_link_libraries(legacy_npc_tests PRIVATE legacy_npc)
  add_test(NAME legacy_npc_acceptance COMMAND legacy_npc_tests)
endif()

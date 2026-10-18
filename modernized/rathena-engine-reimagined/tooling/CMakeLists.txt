cmake_minimum_required(VERSION 3.20)
project(rathena_tooling VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(TOOLING_SANITIZE "Build with ASan/UBSan" OFF)

# One library holding the tool logic; thin CLI drivers link it.
add_library(rathena_tooling STATIC
  src/common.cpp
  src/config.cpp
  src/sim.cpp
  src/migrate.cpp
  src/migrate_saves.cpp
  src/pack.cpp
  src/codegen.cpp
  src/spike.cpp)
target_include_directories(rathena_tooling PUBLIC include)
target_compile_features(rathena_tooling PUBLIC cxx_std_20)
target_compile_options(rathena_tooling PRIVATE -Wall -Wextra -Wpedantic)
if(TOOLING_SANITIZE)
  target_compile_options(rathena_tooling PUBLIC -fsanitize=address,undefined)
  target_link_options(rathena_tooling PUBLIC -fsanitize=address,undefined)
endif()

foreach(tool rathena-sim rathena-migrate rathena-migrate-saves rathena-pack rathena-codegen rathena-spike)
  string(REPLACE "-" "_" src ${tool})
  add_executable(${tool} cli/${src}.cpp)
  target_link_libraries(${tool} PRIVATE rathena_tooling)
endforeach()

enable_testing()
add_executable(tooling_tests
  tests/main.cpp
  tests/test_sim.cpp
  tests/test_migrate.cpp
  tests/test_migrate_saves.cpp
  tests/test_pack_codegen_spike.cpp
  tests/test_security.cpp)
target_link_libraries(tooling_tests PRIVATE rathena_tooling)
target_compile_definitions(tooling_tests PRIVATE
  TOOLING_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
add_test(NAME acceptance COMMAND tooling_tests)

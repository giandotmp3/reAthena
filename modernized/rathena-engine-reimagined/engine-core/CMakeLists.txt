cmake_minimum_required(VERSION 3.20)
project(engine_core VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(ENGINE_CORE_SANITIZE "Build with ASan/UBSan" OFF)

add_library(engine_core STATIC
  src/common.cpp
  src/ecs.cpp
  src/attrs.cpp
  src/grid.cpp
  src/path.cpp
  src/tick.cpp
  src/movement.cpp
  src/instance.cpp)
target_include_directories(engine_core PUBLIC include)
target_compile_features(engine_core PUBLIC cxx_std_20)
target_compile_options(engine_core PRIVATE -Wall -Wextra -Wpedantic)
if(ENGINE_CORE_SANITIZE)
  target_compile_options(engine_core PUBLIC -fsanitize=address,undefined)
  target_link_options(engine_core PUBLIC -fsanitize=address,undefined)
endif()

enable_testing()
add_executable(engine_core_tests
  tests/main.cpp
  tests/test_ecs.cpp
  tests/test_attrs.cpp
  tests/test_grid.cpp
  tests/test_path.cpp
  tests/test_tick.cpp
  tests/test_movement.cpp
  tests/test_instance.cpp
  tests/test_lint.cpp)
target_link_libraries(engine_core_tests PRIVATE engine_core)
target_compile_definitions(engine_core_tests PRIVATE
  ENGINE_CORE_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
add_test(NAME acceptance COMMAND engine_core_tests)

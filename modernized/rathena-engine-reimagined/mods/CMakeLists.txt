cmake_minimum_required(VERSION 3.20)
project(mods VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(MODS_SANITIZE "Build with ASan/UBSan" OFF)

add_library(mods STATIC
  src/ids.cpp
  src/semver.cpp
  src/manifest.cpp
  src/resolver.cpp
  src/lock.cpp
  src/id_map.cpp
  src/loader.cpp
  src/base_check.cpp)
target_include_directories(mods PUBLIC include)
target_compile_features(mods PUBLIC cxx_std_20)
target_compile_options(mods PRIVATE -Wall -Wextra -Wpedantic)
if(MODS_SANITIZE)
  target_compile_options(mods PUBLIC -fsanitize=address,undefined)
  target_link_options(mods PUBLIC -fsanitize=address,undefined)
endif()

enable_testing()
add_executable(mods_tests
  tests/main.cpp
  tests/test_manifest.cpp
  tests/test_resolver.cpp
  tests/test_permissions.cpp
  tests/test_ids.cpp
  tests/test_ops.cpp
  tests/test_base.cpp)
target_link_libraries(mods_tests PRIVATE mods)
target_compile_definitions(mods_tests PRIVATE
  MODS_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
add_test(NAME acceptance COMMAND mods_tests)

cmake_minimum_required(VERSION 3.20)
project(native_defaults VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Separate plug-in library. The engine core never links this target; the host
# loads it as an opt-in plug-in (REIMAGINED_ARCHITECTURE section 1.1).
add_library(native_defaults
  src/stage_id.cpp
  src/registry.cpp
  src/stages_damage.cpp
  src/mob_ai.cpp)
target_include_directories(native_defaults PUBLIC include)
target_compile_features(native_defaults PUBLIC cxx_std_20)
if(MSVC)
  target_compile_options(native_defaults PRIVATE /W4 /WX)
else()
  target_compile_options(native_defaults PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

option(NATIVE_DEFAULTS_BUILD_TESTS "Build acceptance tests" ON)
if(NATIVE_DEFAULTS_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

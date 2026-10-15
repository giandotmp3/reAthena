# rathena-core: shared C++20 library for the reimagined rAthena suite.
#
# Scope (REIMAGINED_ARCHITECTURE.md §2, §4, §10 unit `core`):
#   - config loader with `import:` overlays and ${ENV} placeholders (CAP-26)
#   - secrets from the environment, refusing known-default credentials (S1, BR-INTER-016)
#   - injectable Rng + deterministic seeded implementation (§3.2, §6)
#   - typed immutable BattleConfig (+ inter/char/login policy values) with shipped defaults (spec §5.6)
#   - logging with a token/password redaction filter (S2, BR-WEB-017)
#
# Build:  cmake -B build && cmake --build build && ctest --test-dir build
cmake_minimum_required(VERSION 3.20)
project(rathena_core VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(RATHENA_CORE_BUILD_TESTS "Build rathena-core acceptance tests" ON)
option(RATHENA_CORE_WERROR "Treat warnings as errors" OFF)

add_library(rathena_core
  src/env.cpp
  src/config.cpp
  src/sha256.cpp
  src/secrets.cpp
  src/rng.cpp
  src/typed_config.cpp
  src/logging.cpp
)
add_library(rathena::core ALIAS rathena_core)
target_include_directories(rathena_core PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)
target_compile_features(rathena_core PUBLIC cxx_std_20)

if(MSVC)
  target_compile_options(rathena_core PRIVATE /W4 /permissive-)
  if(RATHENA_CORE_WERROR)
    target_compile_options(rathena_core PRIVATE /WX)
  endif()
else()
  target_compile_options(rathena_core PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
  if(RATHENA_CORE_WERROR)
    target_compile_options(rathena_core PRIVATE -Werror)
  endif()
endif()

if(RATHENA_CORE_BUILD_TESTS)
  enable_testing()
  include(FetchContent)
  # GoogleTest v1.15.2 (pinned to the release tag's commit).
  # Offline builds: -DFETCHCONTENT_SOURCE_DIR_GOOGLETEST=/path/to/googletest
  FetchContent_Declare(googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        b514bdc898e2951020cbdca1304b75f5950d1f59 # v1.15.2
    GIT_SHALLOW    FALSE
  )
  set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
  set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
  set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(googletest)
  add_subdirectory(tests)
endif()

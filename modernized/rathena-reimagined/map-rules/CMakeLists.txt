# rathena-map-rules: pure, IO-free map-server rule library (C++20).
#
# Scope (REIMAGINED_ARCHITECTURE.md §2, §3.1, §3.2, §8, §10 unit `map-rules`):
#   rules/combat       BR-MAP-004..020   (CAP-14; renewal AND pre-renewal pipelines, spec §1.1 "Retained")
#   rules/progression  BR-MAP-021..034   (CAP-15)
#   rules/economy      BR-MAP-001..003, 035..050 (CAP-17, CAP-18 trade/vending)
#   rules/skills       BR-MAP-051..055   (CAP-16; BR-MAP-054 [DEFECT] fixed: a full cooldown
#                                         list never lets a skill bypass its cooldown)
#   rules/anticheat    BR-MAP-056..061   (CAP-20; BR-MAP-056 [DEFECT] fixed: uses prevent_logout)
#   manifest/map_modules.tsv             every legacy src/map/*.cpp -> exactly one §3.1 module
#   manifest/rule_manifest.tsv           BR-ID -> module -> test file (§8 per-rule manifest)
#
# Dependency rule (architecture §2): rules -> domain + {Rng, BattleConfig} only. No sockets, no
# SQL, no globals, no environment reads. Every rule receives its BattleConfig (and Rng& when it
# rolls) explicitly. The legacy map-server call sites delegate here during in-place extraction.
#
# This unit needs no secrets or database at runtime.
#
# Build:  cmake -B build && cmake --build build && ctest --test-dir build
# Offline: -DFETCHCONTENT_SOURCE_DIR_GOOGLETEST=/path/to/googletest-v1.15.2
cmake_minimum_required(VERSION 3.20)
project(rathena_map_rules VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(RATHENA_MAP_RULES_BUILD_TESTS "Build rathena-map-rules acceptance tests" ON)
option(RATHENA_MAP_RULES_WERROR "Treat warnings as errors" OFF)

add_library(rathena_map_rules
  src/domain.cpp
  src/combat.cpp
  src/attr_fix_loader.cpp
  src/progression.cpp
  src/economy.cpp
  src/trade.cpp
  src/skills.cpp
  src/anticheat.cpp
)
add_library(rathena::map_rules ALIAS rathena_map_rules)
target_include_directories(rathena_map_rules PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)
target_compile_features(rathena_map_rules PUBLIC cxx_std_20)

if(MSVC)
  target_compile_options(rathena_map_rules PRIVATE /W4 /permissive-)
  if(RATHENA_MAP_RULES_WERROR)
    target_compile_options(rathena_map_rules PRIVATE /WX)
  endif()
else()
  target_compile_options(rathena_map_rules PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
  if(RATHENA_MAP_RULES_WERROR)
    target_compile_options(rathena_map_rules PRIVATE -Werror)
  endif()
endif()

if(RATHENA_MAP_RULES_BUILD_TESTS)
  enable_testing()
  include(FetchContent)
  # GoogleTest v1.15.2 (pinned to the release tag's commit).
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

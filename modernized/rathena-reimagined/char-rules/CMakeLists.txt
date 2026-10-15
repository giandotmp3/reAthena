# rathena-char-rules: pure, IO-free char-server rule library (C++20).
#
# Scope (REIMAGINED_ARCHITECTURE.md §2, §3, §3.2, §8, §10 unit `char-rules`):
#   rules/charlife  BR-CHAR-001..032, 052, 053
#   rules/party     BR-CHAR-033..036
#   rules/guild     BR-CHAR-037..045
#   rules/mail      BR-CHAR-046..049 (RODEX only; pre-RODEX branch of 049 and auction dropped, spec §1.1)
#   rules/registry  BR-CHAR-050, 054
#   rules/clan      BR-CHAR-051
#   rules/gm        BR-CHAR-055 (authority re-check defect fixed)
#   rules/session   BR-INTER-008, 009, 015, 018, 019
#   save->auth ordering barrier per char_id (architecture §3 char-server row, §9 M2)
#
# Dependency rule (architecture §2): rules -> domain + {Rng, config values} only. No sockets,
# no SQL, no globals. The char-server legacy call sites delegate here during in-place extraction.
#
# Build:  cmake -B build && cmake --build build && ctest --test-dir build
cmake_minimum_required(VERSION 3.20)
project(rathena_char_rules VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(RATHENA_CHAR_RULES_BUILD_TESTS "Build rathena-char-rules acceptance tests" ON)
option(RATHENA_CHAR_RULES_WERROR "Treat warnings as errors" OFF)

add_library(rathena_char_rules
  src/charlife.cpp
  src/pincode.cpp
  src/session.cpp
  src/party.cpp
  src/guild.cpp
  src/mail.cpp
  src/registry.cpp
  src/clan.cpp
  src/gm.cpp
  src/save_auth_barrier.cpp
)
add_library(rathena::char_rules ALIAS rathena_char_rules)
target_include_directories(rathena_char_rules PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)
target_compile_features(rathena_char_rules PUBLIC cxx_std_20)

if(MSVC)
  target_compile_options(rathena_char_rules PRIVATE /W4 /permissive-)
  if(RATHENA_CHAR_RULES_WERROR)
    target_compile_options(rathena_char_rules PRIVATE /WX)
  endif()
else()
  target_compile_options(rathena_char_rules PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
  if(RATHENA_CHAR_RULES_WERROR)
    target_compile_options(rathena_char_rules PRIVATE -Werror)
  endif()
endif()

if(RATHENA_CHAR_RULES_BUILD_TESTS)
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

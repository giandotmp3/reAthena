# rathena-engine persistence service (C++20): generic write-behind store, mod KV, WAL, migrations,
# id_map with tombstones, quarantine. SQLite and PostgreSQL 16+ backends only.
# Build:  cmake -B build && cmake --build build && ctest --test-dir build
# Pending acceptance tests are skipped; run them (they fail until implemented) with
#   RATHENA_PERSIST_RUN_PENDING=1 ctest --test-dir build
cmake_minimum_required(VERSION 3.20)
project(rathena_engine_persistence VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(RATHENA_PERSIST_BUILD_TESTS "Build acceptance tests" ON)
option(RATHENA_PERSIST_WERROR "Treat warnings as errors" OFF)

add_library(rathena_persist
  src/domain.cpp
  src/db_config.cpp
  src/connection_factory.cpp
  src/stubs.cpp)
add_library(rathena::persist ALIAS rathena_persist)
target_include_directories(rathena_persist PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_compile_features(rathena_persist PUBLIC cxx_std_20)
if(MSVC)
  target_compile_options(rathena_persist PRIVATE /W4 /permissive-)
else()
  target_compile_options(rathena_persist PRIVATE -Wall -Wextra -Wpedantic -Wshadow)
  if(RATHENA_PERSIST_WERROR)
    target_compile_options(rathena_persist PRIVATE -Werror)
  endif()
endif()

# Lift plan: SqliteConnection / PostgresConnection come from
# modernized/rathena-reimagined/persistence (src/sqlite_connection.cpp, postgres_connection.cpp)
# and are copied into src/ in a follow-up step; then SQLite (vendored amalgamation) and libpq
# (optional, RATHENA_PERSIST_WITH_POSTGRESQL=AUTO|ON|OFF) are linked here.

if(RATHENA_PERSIST_BUILD_TESTS)
  enable_testing()
  find_package(GTest QUIET)
  if(NOT GTest_FOUND)
    include(FetchContent)
    # Offline builds: -DFETCHCONTENT_SOURCE_DIR_GOOGLETEST=/path/to/googletest
    FetchContent_Declare(googletest
      GIT_REPOSITORY https://github.com/google/googletest.git
      GIT_TAG        b514bdc898e2951020cbdca1304b75f5950d1f59 # v1.15.2
    )
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
  endif()
  include(GoogleTest)
  add_subdirectory(tests)
endif()

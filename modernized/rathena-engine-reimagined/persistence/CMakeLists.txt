# rathena-engine persistence service (C++20): generic write-behind store, mod KV, WAL, migrations,
# id_map with tombstones, quarantine. SQLite and PostgreSQL 16+ backends only.
# Build:  cmake -B build && cmake --build build && ctest --test-dir build
#
# Options
#   RATHENA_PERSIST_WERROR            warnings are errors (default ON)
#   RATHENA_PERSIST_SANITIZE          none | address | thread   (address = ASan+UBSan)
#   RATHENA_PERSIST_WITH_POSTGRESQL   AUTO | ON | OFF  (needs libpq-fe.h at build time)
#   RATHENA_PERSIST_SCRIPT_HOST_DIR   path of the script-host unit (adapter + storage contract)
# Offline GoogleTest: -DFETCHCONTENT_SOURCE_DIR_GOOGLETEST=/path/to/googletest
cmake_minimum_required(VERSION 3.20)
project(rathena_engine_persistence VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(RATHENA_PERSIST_BUILD_TESTS "Build acceptance tests" ON)
option(RATHENA_PERSIST_WERROR "Treat warnings as errors" ON)
set(RATHENA_PERSIST_SANITIZE "none" CACHE STRING "none | address | thread")
set(RATHENA_PERSIST_WITH_POSTGRESQL "AUTO" CACHE STRING "AUTO | ON | OFF")
set(RATHENA_PERSIST_SCRIPT_HOST_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../script-host" CACHE PATH "script-host unit (read-only)")

find_package(Threads REQUIRED)
# The vendored amalgamation of the earlier scaffold is not available offline: link the system
# SQLite (needs sqlite3.h + libsqlite3).
find_package(SQLite3 REQUIRED)

set(_warn "")
if(NOT MSVC)
  set(_warn -Wall -Wextra -Wpedantic -Wshadow)
  if(RATHENA_PERSIST_WERROR)
    list(APPEND _warn -Werror)
  endif()
  if(RATHENA_PERSIST_SANITIZE STREQUAL "address")
    add_compile_options(-fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer -g)
    add_link_options(-fsanitize=address,undefined)
  elseif(RATHENA_PERSIST_SANITIZE STREQUAL "thread")
    add_compile_options(-fsanitize=thread -fno-omit-frame-pointer -g)
    add_link_options(-fsanitize=thread)
  endif()
endif()

add_library(rathena_persist
  src/domain.cpp
  src/db_config.cpp
  src/connection_factory.cpp
  src/sqlite_connection.cpp
  src/connection_wrappers.cpp
  src/codec.cpp
  src/schema.cpp
  src/stores.cpp
  src/wal.cpp
  src/dirty_tracker.cpp
  src/flusher.cpp
  src/writer.cpp)
add_library(rathena::persist ALIAS rathena_persist)
target_include_directories(rathena_persist PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_compile_features(rathena_persist PUBLIC cxx_std_20)
target_link_libraries(rathena_persist PUBLIC Threads::Threads PRIVATE SQLite::SQLite3)
target_compile_options(rathena_persist PRIVATE ${_warn})

# ---- PostgreSQL (optional) ------------------------------------------------------------------
set(RATHENA_PERSIST_HAVE_POSTGRESQL OFF)
if(NOT RATHENA_PERSIST_WITH_POSTGRESQL STREQUAL "OFF")
  find_path(_pq_inc libpq-fe.h PATH_SUFFIXES postgresql)
  find_library(_pq_lib pq)
  if(_pq_inc AND _pq_lib)
    set(RATHENA_PERSIST_HAVE_POSTGRESQL ON)
    target_sources(rathena_persist PRIVATE src/postgres_connection.cpp)
    target_include_directories(rathena_persist PRIVATE ${_pq_inc})
    target_link_libraries(rathena_persist PRIVATE ${_pq_lib})
    target_compile_definitions(rathena_persist PUBLIC RATHENA_PERSIST_HAVE_LIBPQ=1)
  elseif(RATHENA_PERSIST_WITH_POSTGRESQL STREQUAL "ON")
    message(FATAL_ERROR "RATHENA_PERSIST_WITH_POSTGRESQL=ON but libpq-fe.h / libpq were not found")
  else()
    message(STATUS "persistence: libpq headers not found -> PostgreSQL backend not built (tests skip)")
  endif()
endif()

# ---- script-host adapter (optional) ---------------------------------------------------------
set(RATHENA_PERSIST_HAVE_SCRIPT_HOST OFF)
if(EXISTS "${RATHENA_PERSIST_SCRIPT_HOST_DIR}/include/script_host/storage.hpp")
  set(RATHENA_PERSIST_HAVE_SCRIPT_HOST ON)
  add_library(rathena_persist_scripthost STATIC
    src/script_host_adapter.cpp
    "${RATHENA_PERSIST_SCRIPT_HOST_DIR}/src/storage.cpp")  # compiled from the read-only unit
  add_library(rathena::persist_scripthost ALIAS rathena_persist_scripthost)
  target_include_directories(rathena_persist_scripthost PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    "${RATHENA_PERSIST_SCRIPT_HOST_DIR}/include")
  target_link_libraries(rathena_persist_scripthost PUBLIC rathena_persist)
  target_compile_options(rathena_persist_scripthost PRIVATE ${_warn})
endif()

if(RATHENA_PERSIST_BUILD_TESTS)
  enable_testing()
  find_package(GTest QUIET)
  if(NOT GTest_FOUND)
    include(FetchContent)
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

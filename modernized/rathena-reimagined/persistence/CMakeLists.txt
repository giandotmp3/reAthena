# rathena-persistence: shared C++20 persistence library + `rathena-migrate` tool for the
# reimagined rAthena suite.
#
# Scope (REIMAGINED_ARCHITECTURE.md §2, §5, §10 unit `persistence`):
#   - domain values shared by every repository: Item (ONE value type, ONE persistence mapping
#     reused by inventory / cart_inventory / storage / guild_storage / mail_attachments -- D6
#     handled in code), Zeny (checked: 0 <= zeny <= INT32_MAX, BR-MAP-045), strong ids
#   - repositories for Account, Character, Party, Guild, Mail. Concrete MySQL classes plus an
#     interface only because an in-memory fake exists for tests (architecture §9 m6)
#   - connection config from the environment ONLY: ${DATABASE_URL}, ${LOG_DB_URL} (S1)
#   - rathena-migrate: forward-only, idempotent migration plan §5
#       M0 -> M1 -> M6 -> M6b -> M7 -> M2 -> M4 -> M3a -> M3b (M3a/M3b gated)
#
# No live database is needed to build or run the tests.
# Build:  cmake -B build && cmake --build build && ctest --test-dir build
cmake_minimum_required(VERSION 3.20)
project(rathena_persistence VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(RATHENA_PERSISTENCE_BUILD_TESTS "Build rathena-persistence acceptance tests" ON)
option(RATHENA_PERSISTENCE_BUILD_TOOLS "Build the rathena-migrate tool" ON)
option(RATHENA_PERSISTENCE_WERROR "Treat warnings as errors" OFF)

add_library(rathena_persistence
  src/ids.cpp
  src/zeny.cpp
  src/item_mapping.cpp
  src/table_names.cpp
  src/db_config.cpp
  src/in_memory.cpp
  src/mysql.cpp
  src/character_delete.cpp
  src/migrate/schema.cpp
  src/migrate/plan.cpp
  src/migrate/migrator.cpp
)
add_library(rathena::persistence ALIAS rathena_persistence)
target_include_directories(rathena_persistence PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)
target_compile_features(rathena_persistence PUBLIC cxx_std_20)

function(rathena_persistence_warnings target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /permissive-)
    if(RATHENA_PERSISTENCE_WERROR)
      target_compile_options(${target} PRIVATE /WX)
    endif()
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
    if(RATHENA_PERSISTENCE_WERROR)
      target_compile_options(${target} PRIVATE -Werror)
    endif()
  endif()
endfunction()
rathena_persistence_warnings(rathena_persistence)

if(RATHENA_PERSISTENCE_BUILD_TOOLS)
  add_executable(rathena_migrate tools/rathena_migrate_main.cpp)
  set_target_properties(rathena_migrate PROPERTIES OUTPUT_NAME rathena-migrate)
  target_link_libraries(rathena_migrate PRIVATE rathena::persistence)
  rathena_persistence_warnings(rathena_migrate)
endif()

if(RATHENA_PERSISTENCE_BUILD_TESTS)
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

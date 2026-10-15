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
project(rathena_persistence VERSION 0.1.0 LANGUAGES C CXX) # C: the vendored SQLite amalgamation

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
  src/sqlite_connection.cpp
  src/target_schema.cpp
  src/connection_factory.cpp
  src/character_delete.cpp
  src/migrate/schema.cpp
  src/migrate/plan.cpp
  src/migrate/migrator.cpp
  src/migrate/mysql_target.cpp
)
add_library(rathena::persistence ALIAS rathena_persistence)

# SQLite (DATABASE_DECISION.md): vendored amalgamation, always built in, so local development
# and every repository test need no database server on any platform. Pinned to 3.53.4; the
# archive hash below was checked against the sqlite3.c SHA3-256 in the official release log.
# Offline builds: -DFETCHCONTENT_SOURCE_DIR_SQLITE_AMALGAMATION=<dir containing sqlite3.c>.
include(FetchContent)
FetchContent_Declare(sqlite_amalgamation
  URL      https://www.sqlite.org/2026/sqlite-amalgamation-3530400.zip
  URL_HASH SHA3_256=628a44cfe82c66aed1ccbbe85a562d2e33ebe64b3288981ed76285612227934e
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(sqlite_amalgamation) # no CMakeLists inside: this only populates
if(NOT TARGET rathena_sqlite3)
  find_package(Threads REQUIRED)
  add_library(rathena_sqlite3 STATIC ${sqlite_amalgamation_SOURCE_DIR}/sqlite3.c)
  target_include_directories(rathena_sqlite3 SYSTEM PUBLIC ${sqlite_amalgamation_SOURCE_DIR})
  target_compile_definitions(rathena_sqlite3 PRIVATE
    SQLITE_DQS=0                    # "text" is always an identifier, never a string literal
    SQLITE_DEFAULT_FOREIGN_KEYS=1
    SQLITE_THREADSAFE=1
    SQLITE_OMIT_LOAD_EXTENSION      # no runtime extension loading
    SQLITE_OMIT_DEPRECATED
    SQLITE_DEFAULT_MEMSTATUS=0
    SQLITE_LIKE_DOESNT_MATCH_BLOBS)
  target_link_libraries(rathena_sqlite3 PRIVATE Threads::Threads)
  set_target_properties(rathena_sqlite3 PROPERTIES POSITION_INDEPENDENT_CODE ON)
endif()
target_link_libraries(rathena_persistence PRIVATE rathena_sqlite3)

# MySQL/MariaDB client: AUTO uses one if found (MariaDB Connector/C or libmysqlclient), ON
# requires it, OFF always builds the NotImplemented stub.
set(RATHENA_PERSISTENCE_WITH_MYSQL "AUTO" CACHE STRING "Link a MySQL/MariaDB client (AUTO, ON, OFF)")
set_property(CACHE RATHENA_PERSISTENCE_WITH_MYSQL PROPERTY STRINGS AUTO ON OFF)
set(_rathena_mysql_found FALSE)
if(NOT RATHENA_PERSISTENCE_WITH_MYSQL STREQUAL "OFF")
  find_path(RATHENA_MYSQL_INCLUDE_DIR mysql.h PATH_SUFFIXES mariadb mysql)
  find_library(RATHENA_MYSQL_LIBRARY NAMES mariadb mariadbclient mysqlclient libmariadb libmysql)
  if(RATHENA_MYSQL_INCLUDE_DIR AND RATHENA_MYSQL_LIBRARY)
    set(_rathena_mysql_found TRUE)
  elseif(RATHENA_PERSISTENCE_WITH_MYSQL STREQUAL "ON")
    message(FATAL_ERROR "RATHENA_PERSISTENCE_WITH_MYSQL=ON but no MySQL/MariaDB client was found")
  endif()
endif()
if(_rathena_mysql_found)
  message(STATUS "rathena-persistence: MySQL client ${RATHENA_MYSQL_LIBRARY}")
  target_sources(rathena_persistence PRIVATE src/mysql_client.cpp)
  target_include_directories(rathena_persistence PRIVATE ${RATHENA_MYSQL_INCLUDE_DIR})
  target_link_libraries(rathena_persistence PRIVATE ${RATHENA_MYSQL_LIBRARY})
  target_compile_definitions(rathena_persistence PUBLIC RATHENA_PERSISTENCE_HAS_MYSQL_CLIENT=1)
else()
  message(STATUS "rathena-persistence: no MySQL client; MysqlConnection is a stub")
  target_sources(rathena_persistence PRIVATE src/mysql_client_stub.cpp)
endif()
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

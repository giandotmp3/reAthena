# Superbuild for the rathena-reimagined units.
# Each unit is also a standalone CMake project; this file only aggregates them so
# one configure/build/ctest covers everything (and CI has a single entry point).
cmake_minimum_required(VERSION 3.20)
project(rathena_reimagined LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(RATHENA_REIMAGINED_WERROR "Treat warnings as errors in every unit" OFF)
foreach(opt RATHENA_CORE_WERROR RATHENA_PERSISTENCE_WERROR RATHENA_LOGIN_WERROR
            RATHENA_WEB_WERROR RATHENA_CHAR_RULES_WERROR RATHENA_MAP_RULES_WERROR)
  set(${opt} ${RATHENA_REIMAGINED_WERROR} CACHE BOOL "" FORCE)
endforeach()

# GoogleTest is declared by every unit; declare it once here so all units share
# one copy (FetchContent keeps the first declaration).
include(FetchContent)
FetchContent_Declare(googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG        b514bdc898e2951020cbdca1304b75f5950d1f59 # v1.15.2
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)

enable_testing()

set(RATHENA_REIMAGINED_UNITS
  core
  netproto
  persistence
  login-server
  web-server
  char-rules
  map-rules
)
foreach(unit IN LISTS RATHENA_REIMAGINED_UNITS)
  add_subdirectory(${unit})
endforeach()

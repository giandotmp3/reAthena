cmake_minimum_required(VERSION 3.20)
project(script_host VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# --- Luau discovery ---------------------------------------------------------
# SCRIPT_HOST_WITH_LUAU: AUTO (default) links Luau when found, ON requires it,
# OFF builds the pure C++ parts only. Luau-dependent tests report SKIP when the
# VM is not linked. No network access is ever attempted.
#
#   LUAU_ROOT     directory holding VM/include/lua.h and Compiler/include/luacode.h
#                 (default: $RATHENA_LUAU_ROOT)
#   LUAU_LIB_DIR  directory holding libLuau.VM.a / libLuau.Compiler.a / libLuau.Ast.a
#                 (default: $RATHENA_LUAU_LIB_DIR, else searched next to LUAU_ROOT:
#                  ../b, ../build, ./build, ./cmake, ./lib, .)
set(SCRIPT_HOST_WITH_LUAU "AUTO" CACHE STRING "Link the Luau VM: AUTO, ON or OFF")
set_property(CACHE SCRIPT_HOST_WITH_LUAU PROPERTY STRINGS AUTO ON OFF)
set(LUAU_ROOT "$ENV{RATHENA_LUAU_ROOT}" CACHE PATH "Luau source root (VM/include, Compiler/include)")
set(LUAU_LIB_DIR "$ENV{RATHENA_LUAU_LIB_DIR}" CACHE PATH "Directory with libLuau.*.a")
option(SCRIPT_HOST_BUILD_TESTS "Build acceptance tests" ON)

set(SCRIPT_HOST_LUAU_FOUND OFF)
set(SCRIPT_HOST_LUAU_ANALYSIS_FOUND OFF)
if(NOT SCRIPT_HOST_WITH_LUAU STREQUAL "OFF")
  if(LUAU_ROOT AND EXISTS "${LUAU_ROOT}/VM/include/lua.h" AND EXISTS "${LUAU_ROOT}/Compiler/include/luacode.h")
    set(_luau_dirs "${LUAU_LIB_DIR}")
    foreach(_d "../b" "../build" "build" "cmake" "lib" ".")
      list(APPEND _luau_dirs "${LUAU_ROOT}/${_d}")
    endforeach()
    find_library(LUAU_VM_LIB NAMES Luau.VM PATHS ${_luau_dirs} NO_DEFAULT_PATH)
    find_library(LUAU_COMPILER_LIB NAMES Luau.Compiler PATHS ${_luau_dirs} NO_DEFAULT_PATH)
    find_library(LUAU_AST_LIB NAMES Luau.Ast PATHS ${_luau_dirs} NO_DEFAULT_PATH)
    if(LUAU_VM_LIB AND LUAU_COMPILER_LIB AND LUAU_AST_LIB)
      set(SCRIPT_HOST_LUAU_FOUND ON)
    endif()
    # Optional: the analysis stack, used only by the declaration-check test.
    find_library(LUAU_ANALYSIS_LIB NAMES Luau.Analysis PATHS ${_luau_dirs} NO_DEFAULT_PATH)
    find_library(LUAU_CONFIG_LIB NAMES Luau.Config PATHS ${_luau_dirs} NO_DEFAULT_PATH)
    find_library(LUAU_EQSAT_LIB NAMES Luau.EqSat PATHS ${_luau_dirs} NO_DEFAULT_PATH)
    if(LUAU_ANALYSIS_LIB AND LUAU_CONFIG_LIB AND LUAU_EQSAT_LIB AND EXISTS "${LUAU_ROOT}/Analysis/include/Luau/Frontend.h")
      set(SCRIPT_HOST_LUAU_ANALYSIS_FOUND ON)
    endif()
  endif()
  if(NOT SCRIPT_HOST_LUAU_FOUND)
    if(SCRIPT_HOST_WITH_LUAU STREQUAL "ON")
      message(FATAL_ERROR "SCRIPT_HOST_WITH_LUAU=ON but Luau was not found. Set LUAU_ROOT (or RATHENA_LUAU_ROOT) and LUAU_LIB_DIR.")
    endif()
    message(STATUS "script-host: Luau not found (LUAU_ROOT='${LUAU_ROOT}'); building without the VM. Luau-dependent tests will be SKIPPED.")
  else()
    message(STATUS "script-host: Luau VM found under ${LUAU_ROOT}")
  endif()
endif()

add_library(script_host
  src/ids.cpp
  src/event_bus.cpp
  src/pipeline.cpp
  src/effect_queue.cpp
  src/budget.cpp
  src/sandbox.cpp
  src/dialogue.cpp
  src/reload.cpp
  src/storage.cpp
  src/numeric.cpp
  src/api_schema.cpp
  src/yaml_lite.cpp)
target_include_directories(script_host PUBLIC include)
target_compile_features(script_host PUBLIC cxx_std_20)
if(MSVC)
  target_compile_options(script_host PRIVATE /W4 /WX)
else()
  target_compile_options(script_host PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

if(SCRIPT_HOST_LUAU_FOUND)
  target_include_directories(script_host SYSTEM PUBLIC
    "${LUAU_ROOT}/VM/include" "${LUAU_ROOT}/Compiler/include")
  target_link_libraries(script_host PUBLIC "${LUAU_COMPILER_LIB}" "${LUAU_AST_LIB}" "${LUAU_VM_LIB}")
  target_compile_definitions(script_host PUBLIC SCRIPT_HOST_WITH_LUAU=1)
endif()

# Generator: api/engine.yaml -> api/engine.d.luau (checked in) and C++ bindings.
add_executable(gen_api tools/gen_api.cpp)
target_link_libraries(gen_api PRIVATE script_host)
if(NOT MSVC)
  target_compile_options(gen_api PRIVATE -Wall -Wextra -Werror)
endif()
add_custom_target(generate_api
  COMMAND gen_api "${CMAKE_CURRENT_SOURCE_DIR}/api/engine.yaml"
          --luau "${CMAKE_CURRENT_SOURCE_DIR}/api/engine.d.luau"
          --cpp "${CMAKE_CURRENT_BINARY_DIR}/engine_api_bindings.hpp"
  DEPENDS gen_api "${CMAKE_CURRENT_SOURCE_DIR}/api/engine.yaml"
  COMMENT "Regenerating api/engine.d.luau from api/engine.yaml")

if(SCRIPT_HOST_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

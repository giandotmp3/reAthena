cmake_minimum_required(VERSION 3.20)
project(mods VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(MODS_SANITIZE "Build mods itself with ASan/UBSan (for the whole tree pass -DCMAKE_CXX_FLAGS=-fsanitize=address,undefined)" OFF)
set(MODS_WITH_HOST "AUTO" CACHE STRING "Build the script-host integration (mods_host): AUTO, ON, OFF")
set_property(CACHE MODS_WITH_HOST PROPERTY STRINGS AUTO ON OFF)
set(MODS_SCRIPT_HOST_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../script-host" CACHE PATH "script-host source directory (read-only reference)")

# ---- core: no VM, no persistence dependency ---------------------------------
add_library(mods STATIC
  src/toml.cpp
  src/ids.cpp
  src/semver.cpp
  src/sha256.cpp
  src/manifest.cpp
  src/resolver.cpp
  src/lock.cpp
  src/id_map.cpp
  src/loader.cpp
  src/base_check.cpp)
target_include_directories(mods PUBLIC include)
target_compile_features(mods PUBLIC cxx_std_20)
if(MSVC)
  target_compile_options(mods PRIVATE /W4 /WX)
else()
  target_compile_options(mods PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
if(MODS_SANITIZE)
  target_compile_options(mods PUBLIC -fsanitize=address,undefined -fno-sanitize-recover=undefined)
  target_link_options(mods PUBLIC -fsanitize=address,undefined)
endif()

# ---- integration with script-host (ReloadManager / ModSandbox) ---------------
set(MODS_HOST_ON OFF)
if(NOT MODS_WITH_HOST STREQUAL "OFF")
  if(EXISTS "${MODS_SCRIPT_HOST_DIR}/CMakeLists.txt")
    set(SCRIPT_HOST_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    add_subdirectory("${MODS_SCRIPT_HOST_DIR}" script-host-build EXCLUDE_FROM_ALL)
    set(MODS_HOST_ON ON)
  elseif(MODS_WITH_HOST STREQUAL "ON")
    message(FATAL_ERROR "MODS_WITH_HOST=ON but ${MODS_SCRIPT_HOST_DIR} has no CMakeLists.txt")
  endif()
endif()
if(MODS_HOST_ON)
  add_library(mods_host STATIC src/host_runtime.cpp)
  target_link_libraries(mods_host PUBLIC mods script_host)
  if(MSVC)
    target_compile_options(mods_host PRIVATE /W4 /WX)
  else()
    target_compile_options(mods_host PRIVATE -Wall -Wextra -Wpedantic -Werror)
  endif()
endif()

# ---- command line tool (CI: check-base, manifest, resolve, digest) ------------
add_executable(mods_tool tools/mods_tool.cpp)
target_link_libraries(mods_tool PRIVATE mods)
if(MSVC)
  target_compile_options(mods_tool PRIVATE /W4 /WX)
else()
  target_compile_options(mods_tool PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

# ---- tests ---------------------------------------------------------------------
enable_testing()
set(MODS_TEST_SOURCES
  tests/main.cpp
  tests/test_toml.cpp
  tests/test_manifest.cpp
  tests/test_resolver.cpp
  tests/test_permissions.cpp
  tests/test_ids.cpp
  tests/test_ops.cpp
  tests/test_base.cpp)
if(MODS_HOST_ON)
  list(APPEND MODS_TEST_SOURCES tests/test_integration.cpp tests/test_pipeline_host.cpp)
endif()
add_executable(mods_tests ${MODS_TEST_SOURCES})
target_link_libraries(mods_tests PRIVATE mods)
if(MODS_HOST_ON)
  target_link_libraries(mods_tests PRIVATE mods_host)
  target_compile_definitions(mods_tests PRIVATE MODS_HAVE_HOST=1)
endif()
if(MSVC)
  target_compile_options(mods_tests PRIVATE /W4 /WX)
else()
  target_compile_options(mods_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
get_filename_component(MODS_ENGINE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
target_compile_definitions(mods_tests PRIVATE
  MODS_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
  MODS_ENGINE_ROOT="${MODS_ENGINE_ROOT}"
  MODS_TOOL_PATH="$<TARGET_FILE:mods_tool>")
add_dependencies(mods_tests mods_tool)
add_test(NAME acceptance COMMAND mods_tests)
# CI gate: rathena.base must not name anything a ruleset defines.
add_test(NAME base_purity
  COMMAND mods_tool check-base --base "${CMAKE_CURRENT_SOURCE_DIR}/base/rathena.base"
          --ruleset "${MODS_ENGINE_ROOT}/ruleset-prerenewal" --ruleset "${MODS_ENGINE_ROOT}/ruleset-renewal"
          --migrate-out "${MODS_ENGINE_ROOT}/tooling/migrate/out/prerenewal"
          --migrate-out "${MODS_ENGINE_ROOT}/tooling/migrate/out/renewal"
          --allow "${CMAKE_CURRENT_SOURCE_DIR}/config/base_allowed_words.txt")

cmake_minimum_required(VERSION 3.20)
project(rathena_tooling VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(TOOLING_SANITIZE "Build the tooling targets with ASan/UBSan (for the whole tree pass -DCMAKE_CXX_FLAGS=-fsanitize=address,undefined)" OFF)
option(TOOLING_WERROR "Warnings are errors in the tooling sources" ON)

# ---------------------------------------------------------------------------------------------
# rathena-sim links the sibling units (read-only references): engine-core, script-host (+ Luau),
# mods, persistence (SQLite) and netproto (codecs + generated catalog). gateway is NOT used.
#   export RATHENA_LUAU_ROOT=/path/to/luau/src RATHENA_LUAU_LIB_DIR=/path/to/luau/build
# Without Luau the build still succeeds; every sim test that needs the VM reports SKIP.
# ---------------------------------------------------------------------------------------------
get_filename_component(ENGINE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
foreach(unit engine-core script-host mods persistence netproto)
  if(NOT EXISTS "${ENGINE_ROOT}/${unit}/CMakeLists.txt")
    message(FATAL_ERROR "rathena-sim needs the sibling unit '${unit}' at ${ENGINE_ROOT}/${unit}")
  endif()
endforeach()

set(SCRIPT_HOST_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory("${ENGINE_ROOT}/script-host" script-host EXCLUDE_FROM_ALL)

find_package(Threads REQUIRED)
find_package(SQLite3 REQUIRED)
find_package(Python3 REQUIRED COMPONENTS Interpreter)

# Units compiled from their own sources (each has its own add_subdirectory-hostile CMake file).
set(_unit_warn -Wall -Wextra -Wpedantic)

add_library(sim_engine_core STATIC
  ${ENGINE_ROOT}/engine-core/src/common.cpp
  ${ENGINE_ROOT}/engine-core/src/ecs.cpp
  ${ENGINE_ROOT}/engine-core/src/attrs.cpp
  ${ENGINE_ROOT}/engine-core/src/grid.cpp
  ${ENGINE_ROOT}/engine-core/src/path.cpp
  ${ENGINE_ROOT}/engine-core/src/tick.cpp
  ${ENGINE_ROOT}/engine-core/src/movement.cpp
  ${ENGINE_ROOT}/engine-core/src/instance.cpp)
target_include_directories(sim_engine_core SYSTEM PUBLIC ${ENGINE_ROOT}/engine-core/include)
target_compile_options(sim_engine_core PRIVATE ${_unit_warn})

add_library(sim_mods STATIC
  ${ENGINE_ROOT}/mods/src/toml.cpp
  ${ENGINE_ROOT}/mods/src/ids.cpp
  ${ENGINE_ROOT}/mods/src/semver.cpp
  ${ENGINE_ROOT}/mods/src/sha256.cpp
  ${ENGINE_ROOT}/mods/src/manifest.cpp
  ${ENGINE_ROOT}/mods/src/resolver.cpp
  ${ENGINE_ROOT}/mods/src/lock.cpp
  ${ENGINE_ROOT}/mods/src/id_map.cpp
  ${ENGINE_ROOT}/mods/src/loader.cpp
  ${ENGINE_ROOT}/mods/src/host_runtime.cpp)
target_include_directories(sim_mods SYSTEM PUBLIC ${ENGINE_ROOT}/mods/include)
target_link_libraries(sim_mods PUBLIC script_host)
target_compile_options(sim_mods PRIVATE ${_unit_warn})

# persistence: the SQLite connection, stores and the script-host adapter. script-host's
# storage.cpp comes from the script_host library, so it is not compiled a second time.
add_library(sim_persist STATIC
  ${ENGINE_ROOT}/persistence/src/domain.cpp
  ${ENGINE_ROOT}/persistence/src/db_config.cpp
  ${ENGINE_ROOT}/persistence/src/connection_factory.cpp
  ${ENGINE_ROOT}/persistence/src/sqlite_connection.cpp
  ${ENGINE_ROOT}/persistence/src/connection_wrappers.cpp
  ${ENGINE_ROOT}/persistence/src/codec.cpp
  ${ENGINE_ROOT}/persistence/src/schema.cpp
  ${ENGINE_ROOT}/persistence/src/stores.cpp
  ${ENGINE_ROOT}/persistence/src/wal.cpp
  ${ENGINE_ROOT}/persistence/src/dirty_tracker.cpp
  ${ENGINE_ROOT}/persistence/src/flusher.cpp
  ${ENGINE_ROOT}/persistence/src/writer.cpp
  ${ENGINE_ROOT}/persistence/src/script_host_adapter.cpp)
target_include_directories(sim_persist SYSTEM PUBLIC ${ENGINE_ROOT}/persistence/include)
target_link_libraries(sim_persist PUBLIC script_host Threads::Threads PRIVATE SQLite::SQLite3)
target_compile_options(sim_persist PRIVATE ${_unit_warn})

add_library(sim_netproto STATIC
  ${ENGINE_ROOT}/netproto/src/common.cpp
  ${ENGINE_ROOT}/netproto/src/spec.cpp
  ${ENGINE_ROOT}/netproto/src/pos_codec.cpp
  ${ENGINE_ROOT}/netproto/src/obfuscation.cpp
  ${ENGINE_ROOT}/netproto/src/framing.cpp
  ${ENGINE_ROOT}/netproto/src/gating.cpp
  ${ENGINE_ROOT}/netproto/src/flood_guard.cpp
  ${ENGINE_ROOT}/netproto/src/connect_check.cpp
  ${ENGINE_ROOT}/netproto/src/codec_set.cpp
  ${ENGINE_ROOT}/netproto/src/sim_client.cpp)
target_include_directories(sim_netproto SYSTEM PUBLIC ${ENGINE_ROOT}/netproto/include)
target_compile_options(sim_netproto PRIVATE ${_unit_warn})

# The full netproto catalog of the real client (1,186 packets) is generated exactly as netproto's own build does
# (Python 3 + PyYAML), into this build tree; nothing is written inside the netproto unit.
execute_process(COMMAND ${Python3_EXECUTABLE} -I -c "import yaml"
                RESULT_VARIABLE _yaml_rc OUTPUT_QUIET ERROR_QUIET)
if(NOT _yaml_rc EQUAL 0)
  message(FATAL_ERROR "rathena-sim: the netproto catalog needs PyYAML for ${Python3_EXECUTABLE}")
endif()
set(SIM_GEN_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
set(NETPROTO_CODEGEN ${ENGINE_ROOT}/netproto/tools/codegen/netproto_codegen.py)
set(NETPROTO_CATALOG ${ENGINE_ROOT}/netproto/proto/packets_re_20211103.yaml)
add_custom_command(
  OUTPUT ${SIM_GEN_DIR}/catalog_packets_re_20211103.hpp
  COMMAND ${Python3_EXECUTABLE} -I ${NETPROTO_CODEGEN} --proto ${NETPROTO_CATALOG}
          --emit-catalog --out ${SIM_GEN_DIR}
  DEPENDS ${NETPROTO_CATALOG} ${NETPROTO_CODEGEN}
  COMMENT "rathena-sim: netproto catalog from packets_re_20211103.yaml"
  VERBATIM)
add_custom_target(sim_generated DEPENDS ${SIM_GEN_DIR}/catalog_packets_re_20211103.hpp)

# The Luau helpers embedded into the binary (scenario prelude), regenerated when the file changes.
set(SIM_PRELUDE_SRC ${CMAKE_CURRENT_SOURCE_DIR}/sim/scenario_prelude.luau)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${SIM_PRELUDE_SRC})
file(READ ${SIM_PRELUDE_SRC} SIM_PRELUDE_TEXT)
configure_file(sim/embed_prelude.cpp.in ${SIM_GEN_DIR}/sim_prelude.cpp @ONLY)

# ---------------------------------------------------------------------------------------------
# One library holding the tool logic; thin CLI drivers link it.
# ---------------------------------------------------------------------------------------------
add_library(rathena_tooling STATIC
  src/common.cpp
  src/config.cpp
  src/sim.cpp
  src/sim_wire.cpp
  src/sim_value.cpp
  src/sim_world.cpp
  src/sim_server.cpp
  src/sim_player.cpp
  src/sim_scenario.cpp
  src/sim_coverage.cpp
  src/sim_conformance.cpp
  src/sim_cli.cpp
  ${SIM_GEN_DIR}/sim_prelude.cpp
  src/migrate.cpp
  src/migrate_saves.cpp
  src/pack.cpp
  src/codegen.cpp
  src/spike.cpp)
add_dependencies(rathena_tooling sim_generated)
target_include_directories(rathena_tooling PUBLIC include)
target_include_directories(rathena_tooling PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_compile_features(rathena_tooling PUBLIC cxx_std_20)
target_link_libraries(rathena_tooling PUBLIC sim_mods sim_persist sim_netproto sim_engine_core script_host
                      Threads::Threads)
target_compile_definitions(rathena_tooling PRIVATE TOOLING_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_options(rathena_tooling PRIVATE -Wall -Wextra -Wpedantic)
if(TOOLING_WERROR)
  target_compile_options(rathena_tooling PRIVATE -Werror)
endif()
if(TOOLING_SANITIZE)
  target_compile_options(rathena_tooling PUBLIC -fsanitize=address,undefined -fno-sanitize-recover=undefined)
  target_link_options(rathena_tooling PUBLIC -fsanitize=address,undefined)
endif()

foreach(tool rathena-sim rathena-migrate rathena-migrate-saves rathena-pack rathena-codegen rathena-spike)
  string(REPLACE "-" "_" src ${tool})
  add_executable(${tool} cli/${src}.cpp)
  target_link_libraries(${tool} PRIVATE rathena_tooling)
  target_compile_options(${tool} PRIVATE -Wall -Wextra -Wpedantic)
  if(TOOLING_WERROR)
    target_compile_options(${tool} PRIVATE -Werror)
  endif()
endforeach()
target_compile_definitions(rathena-sim PRIVATE TOOLING_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")

enable_testing()
add_executable(tooling_tests
  tests/main.cpp
  tests/test_sim.cpp
  tests/test_sim_wire.cpp
  tests/test_sim_world.cpp
  tests/test_sim_players.cpp
  tests/test_sim_scenarios.cpp
  tests/test_sim_coverage.cpp
  tests/test_sim_conformance.cpp
  tests/test_migrate.cpp
  tests/test_migrate_saves.cpp
  tests/test_pack_codegen_spike.cpp
  tests/test_security.cpp)
target_link_libraries(tooling_tests PRIVATE rathena_tooling)
target_compile_options(tooling_tests PRIVATE -Wall -Wextra -Wpedantic)
if(TOOLING_WERROR)
  target_compile_options(tooling_tests PRIVATE -Werror)
endif()
target_include_directories(tooling_tests PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_compile_definitions(tooling_tests PRIVATE
  TOOLING_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
  TOOLING_SIM_BIN="$<TARGET_FILE:rathena-sim>")
add_dependencies(tooling_tests rathena-sim)
add_test(NAME acceptance COMMAND tooling_tests)

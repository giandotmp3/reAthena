cmake_minimum_required(VERSION 3.20)
project(netproto VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(NETPROTO_SANITIZE "Build with ASan/UBSan" OFF)
option(NETPROTO_FUZZ "Build libFuzzer targets (clang only)" OFF)
option(NETPROTO_WERROR "Treat warnings as errors" ON)

add_library(netproto STATIC
  src/common.cpp
  src/spec.cpp
  src/pos_codec.cpp
  src/obfuscation.cpp
  src/framing.cpp
  src/gating.cpp
  src/flood_guard.cpp
  src/connect_check.cpp
  src/codec_set.cpp
  src/yaml.cpp
  src/spec_yaml.cpp
  src/sim_client.cpp)
target_include_directories(netproto PUBLIC include)
target_compile_features(netproto PUBLIC cxx_std_20)
target_compile_options(netproto PRIVATE -Wall -Wextra -Wpedantic)
if(NETPROTO_WERROR)
  target_compile_options(netproto PRIVATE -Werror)
endif()
if(NETPROTO_SANITIZE)
  target_compile_options(netproto PUBLIC -fsanitize=address,undefined)
  target_link_options(netproto PUBLIC -fsanitize=address,undefined)
endif()

enable_testing()

# Every executable of this unit builds warning-free (-Wall -Wextra -Wpedantic [-Werror]).
function(netproto_strict target)
  target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
  if(NETPROTO_WERROR)
    target_compile_options(${target} PRIVATE -Werror)
  endif()
endfunction()

# Generated from proto/*.yaml by tools/codegen/netproto_codegen.py (needs Python 3 + PyYAML).
find_package(Python3 REQUIRED COMPONENTS Interpreter)
execute_process(COMMAND ${Python3_EXECUTABLE} -I -c "import yaml"
                RESULT_VARIABLE NETPROTO_YAML_RC OUTPUT_QUIET ERROR_QUIET)
if(NOT NETPROTO_YAML_RC EQUAL 0)
  message(FATAL_ERROR "netproto codegen needs PyYAML for ${Python3_EXECUTABLE} (pip install pyyaml)")
endif()
set(NETPROTO_GEN_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
set(NETPROTO_CODEGEN ${CMAKE_CURRENT_SOURCE_DIR}/tools/codegen/netproto_codegen.py)
set(NETPROTO_SPEC ${CMAKE_CURRENT_SOURCE_DIR}/proto/client_packets.yaml)
# The full catalog (every typed legacy struct, handler-derived layouts, length-only frames), derived
# from the legacy sources by tools/legacy/derive_catalog.py and checked in; see README.
set(NETPROTO_CATALOG ${CMAKE_CURRENT_SOURCE_DIR}/proto/packets_re_20211103.yaml)
add_custom_command(
  OUTPUT ${NETPROTO_GEN_DIR}/catalog_client_packets.hpp
         ${NETPROTO_GEN_DIR}/catalog_packets_re_20211103.hpp
         ${NETPROTO_GEN_DIR}/frames_re_20211103.hpp
         ${NETPROTO_GEN_DIR}/frames_main_20211103.hpp
  COMMAND ${Python3_EXECUTABLE} -I ${NETPROTO_CODEGEN} --proto ${NETPROTO_SPEC}
          --emit-catalog --client re/20211103 --out ${NETPROTO_GEN_DIR}
  COMMAND ${Python3_EXECUTABLE} -I ${NETPROTO_CODEGEN} --proto ${NETPROTO_SPEC}
          --client main/20211103 --out ${NETPROTO_GEN_DIR}
  COMMAND ${Python3_EXECUTABLE} -I ${NETPROTO_CODEGEN} --proto ${NETPROTO_CATALOG}
          --emit-catalog --out ${NETPROTO_GEN_DIR}
  DEPENDS ${NETPROTO_SPEC} ${NETPROTO_CATALOG} ${NETPROTO_CODEGEN}
  COMMENT "netproto codegen: catalog and frame tables from client_packets.yaml"
  VERBATIM)
add_custom_target(netproto_generated DEPENDS ${NETPROTO_GEN_DIR}/catalog_client_packets.hpp
                                             ${NETPROTO_GEN_DIR}/catalog_packets_re_20211103.hpp)

add_executable(netproto_tests
  tests/main.cpp
  tests/test_pos_codec.cpp
  tests/test_framing.cpp
  tests/test_gating.cpp
  tests/test_connect_check.cpp
  tests/test_flood_guard.cpp
  tests/test_spec_model.cpp
  tests/test_codec_set.cpp
  tests/test_yaml.cpp
  tests/test_codec_fields.cpp
  tests/test_full_catalog.cpp
  tests/test_golden.cpp
  tests/test_fuzz.cpp
  tests/test_obfuscation.cpp
  tests/test_sim_client.cpp
  tests/test_secrets.cpp
  tests/test_legacy_spec.cpp)
add_dependencies(netproto_tests netproto_generated)
target_include_directories(netproto_tests PRIVATE ${NETPROTO_GEN_DIR}/..)
target_link_libraries(netproto_tests PRIVATE netproto)
target_compile_definitions(netproto_tests PRIVATE
  NETPROTO_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
netproto_strict(netproto_tests)
add_test(NAME acceptance COMMAND netproto_tests)

# (The byte-identity compat tests against the gateway's and the sim's former private codecs were removed after both units
# migrated onto this codec; the gateway has golden tests against the real legacy structs through netproto instead.)

# The YAML reader against PyYAML on every proto file.
add_executable(netproto_yaml_dump tests/yaml_dump.cpp)
target_link_libraries(netproto_yaml_dump PRIVATE netproto)
netproto_strict(netproto_yaml_dump)
add_test(NAME yaml_differential
         COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_yaml_differential.py
                 $<TARGET_FILE:netproto_yaml_dump> ${CMAKE_CURRENT_SOURCE_DIR}/proto)

# The spec must equal what the legacy headers say (re-evaluated on every run).
add_test(NAME verify_legacy
         COMMAND ${Python3_EXECUTABLE} -I ${NETPROTO_CODEGEN} --proto ${NETPROTO_SPEC} --verify-legacy)
# Evaluator unit tests, determinism, negative tests (a mutated spec must fail), and the g++ -E /
# compiled cross-check (skipped when g++ is missing).
add_test(NAME legacy_tools
         COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_legacy_tools.py)
# Reference harnesses: tiny programs holding copies of the legacy routines (obfuscation key schedule,
# WBUFPOS/WBUFPOS2). Their output is checked in as the vectors the unit tests use; ctest re-runs them.
add_executable(netproto_obf_ref tests/legacy_ref/obfuscation_ref.cpp)
add_executable(netproto_pos_ref tests/legacy_ref/pos_ref.cpp)
netproto_strict(netproto_obf_ref)
netproto_strict(netproto_pos_ref)
add_test(NAME legacy_ref_obfuscation COMMAND netproto_obf_ref --check ${CMAKE_CURRENT_SOURCE_DIR}/tests/legacy_ref/obf_vectors.inc)
add_test(NAME legacy_ref_positions COMMAND netproto_pos_ref --check ${CMAKE_CURRENT_SOURCE_DIR}/tests/legacy_ref/pos_vectors.inc)
# Golden vectors: every typed legacy struct, filled and dumped by the real compiler (needs g++ and the legacy tree).
add_test(NAME layout_oracle
         COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/tools/legacy/layout_oracle.py
                 --legacy-root ${CMAKE_CURRENT_SOURCE_DIR}/../../.. --work-dir ${CMAKE_CURRENT_BINARY_DIR}/oracle
                 --check ${CMAKE_CURRENT_SOURCE_DIR}/tests/golden/typed_re_20211103.txt)

# The derived full catalog equals a fresh derivation, is internally consistent and contains the overlay.
add_test(NAME verify_catalog
         COMMAND ${Python3_EXECUTABLE} -I ${NETPROTO_CODEGEN} --proto ${NETPROTO_CATALOG} --verify-catalog)
add_test(NAME catalog_tools
         COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_catalog_tools.py)
set_tests_properties(verify_legacy legacy_tools verify_catalog catalog_tools layout_oracle PROPERTIES TIMEOUT 600)

if(NETPROTO_FUZZ)
  add_executable(fuzz_peek_frame fuzz/fuzz_peek_frame.cpp)
  target_link_libraries(fuzz_peek_frame PRIVATE netproto)
  target_compile_options(fuzz_peek_frame PRIVATE -fsanitize=fuzzer,address,undefined)
  target_link_options(fuzz_peek_frame PRIVATE -fsanitize=fuzzer,address,undefined)
  # codec / framing / sim client: the invariants of fuzz/decode_invariants.hpp
  add_executable(fuzz_decode fuzz/fuzz_decode.cpp)
  add_dependencies(fuzz_decode netproto_generated)
  target_include_directories(fuzz_decode PRIVATE ${NETPROTO_GEN_DIR}/.. fuzz)
  target_link_libraries(fuzz_decode PRIVATE netproto)
  netproto_strict(fuzz_decode)
  target_compile_options(fuzz_decode PRIVATE -fsanitize=fuzzer,address,undefined)
  target_link_options(fuzz_decode PRIVATE -fsanitize=fuzzer,address,undefined)
endif()

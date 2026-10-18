cmake_minimum_required(VERSION 3.20)
project(netproto VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(NETPROTO_SANITIZE "Build with ASan/UBSan" OFF)
option(NETPROTO_FUZZ "Build libFuzzer targets (clang only)" OFF)

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
  src/sim_client.cpp)
target_include_directories(netproto PUBLIC include)
target_compile_features(netproto PUBLIC cxx_std_20)
target_compile_options(netproto PRIVATE -Wall -Wextra -Wpedantic)
if(NETPROTO_SANITIZE)
  target_compile_options(netproto PUBLIC -fsanitize=address,undefined)
  target_link_options(netproto PUBLIC -fsanitize=address,undefined)
endif()

enable_testing()

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
add_custom_command(
  OUTPUT ${NETPROTO_GEN_DIR}/catalog_client_packets.hpp
         ${NETPROTO_GEN_DIR}/frames_re_20211103.hpp
         ${NETPROTO_GEN_DIR}/frames_main_20211103.hpp
  COMMAND ${Python3_EXECUTABLE} -I ${NETPROTO_CODEGEN} --proto ${NETPROTO_SPEC}
          --emit-catalog --client re/20211103 --out ${NETPROTO_GEN_DIR}
  COMMAND ${Python3_EXECUTABLE} -I ${NETPROTO_CODEGEN} --proto ${NETPROTO_SPEC}
          --client main/20211103 --out ${NETPROTO_GEN_DIR}
  DEPENDS ${NETPROTO_SPEC} ${NETPROTO_CODEGEN}
  COMMENT "netproto codegen: catalog and frame tables from client_packets.yaml"
  VERBATIM)
add_custom_target(netproto_generated DEPENDS ${NETPROTO_GEN_DIR}/catalog_client_packets.hpp)

add_executable(netproto_tests
  tests/main.cpp
  tests/test_pos_codec.cpp
  tests/test_framing.cpp
  tests/test_gating.cpp
  tests/test_connect_check.cpp
  tests/test_flood_guard.cpp
  tests/test_spec_model.cpp
  tests/test_codec_set.cpp
  tests/test_obfuscation.cpp
  tests/test_sim_client.cpp
  tests/test_secrets.cpp
  tests/test_legacy_spec.cpp)
add_dependencies(netproto_tests netproto_generated)
target_include_directories(netproto_tests PRIVATE ${NETPROTO_GEN_DIR}/..)
target_link_libraries(netproto_tests PRIVATE netproto)
target_compile_definitions(netproto_tests PRIVATE
  NETPROTO_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
add_test(NAME acceptance COMMAND netproto_tests)

# The spec must equal what the legacy headers say (re-evaluated on every run).
add_test(NAME verify_legacy
         COMMAND ${Python3_EXECUTABLE} -I ${NETPROTO_CODEGEN} --proto ${NETPROTO_SPEC} --verify-legacy)
# Evaluator unit tests, determinism, negative tests (a mutated spec must fail), and the g++ -E /
# compiled cross-check (skipped when g++ is missing).
add_test(NAME legacy_tools
         COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_legacy_tools.py)
set_tests_properties(verify_legacy legacy_tools PROPERTIES TIMEOUT 600)

if(NETPROTO_FUZZ)
  add_executable(fuzz_peek_frame fuzz/fuzz_peek_frame.cpp)
  target_link_libraries(fuzz_peek_frame PRIVATE netproto)
  target_compile_options(fuzz_peek_frame PRIVATE -fsanitize=fuzzer,address,undefined)
  target_link_options(fuzz_peek_frame PRIVATE -fsanitize=fuzzer,address,undefined)
endif()

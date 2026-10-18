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
  tests/test_secrets.cpp)
target_link_libraries(netproto_tests PRIVATE netproto)
target_compile_definitions(netproto_tests PRIVATE
  NETPROTO_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
add_test(NAME acceptance COMMAND netproto_tests)

if(NETPROTO_FUZZ)
  add_executable(fuzz_peek_frame fuzz/fuzz_peek_frame.cpp)
  target_link_libraries(fuzz_peek_frame PRIVATE netproto)
  target_compile_options(fuzz_peek_frame PRIVATE -fsanitize=fuzzer,address,undefined)
  target_link_options(fuzz_peek_frame PRIVATE -fsanitize=fuzzer,address,undefined)
endif()

# rathena-reimagined :: netproto
#
# Standalone scaffold (Phase E staging area). Moves to src/netproto once its
# acceptance tests are green on all four CI toolchains (MSVC, gcc, clang, macOS).
#
# Scope: little-endian framing + length validation (S4, BR-INTER-012/013),
# unknown-packet handling (BR-INTER-002), legacy inter-server codec with a
# protocol version field (BR-INTER-001/007, BR-LOGIN-025, BR-CHAR-030),
# widened group_id in 0x2717 (BR-INTER-003), stall timeout and connection-flood
# guard policies (BR-INTER-010/011).
#
# netproto never reads secrets itself: inter-server credentials are injected by
# core/secrets (BR-INTER-016), which reads ${INTER_SERVER_USER} / ${INTER_SERVER_PASS}.

cmake_minimum_required(VERSION 3.20)
project(rathena_netproto VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(NETPROTO_BUILD_TESTS "Build the netproto acceptance tests" ON)
option(NETPROTO_USE_SYSTEM_GTEST "Use an installed GoogleTest instead of FetchContent" OFF)

add_library(rathena_netproto
  src/frame.cpp
  src/buffer_limits.cpp
  src/link_policy.cpp
  src/stall_policy.cpp
  src/flood_guard.cpp
  src/link_session.cpp
  src/inter/handshake.cpp
  src/inter/account_data.cpp
  src/inter/length_tables.cpp
)
add_library(rathena::netproto ALIAS rathena_netproto)

target_include_directories(rathena_netproto PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_compile_features(rathena_netproto PUBLIC cxx_std_20)

if(MSVC)
  target_compile_options(rathena_netproto PRIVATE /W4 /permissive-)
else()
  target_compile_options(rathena_netproto PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
endif()

if(NETPROTO_BUILD_TESTS)
  enable_testing()

  if(NETPROTO_USE_SYSTEM_GTEST)
    find_package(GTest REQUIRED)
  else()
    include(FetchContent)
    FetchContent_Declare(
      googletest
      GIT_REPOSITORY https://github.com/google/googletest.git
      GIT_TAG        v1.15.2
      GIT_SHALLOW    TRUE
    )
    # MSVC: keep the CRT consistent with the parent project.
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
  endif()

  # One file per rule group; every test name carries its BR-ID
  # (BR-INTER-013 -> BR_INTER_013_..., since gtest names cannot contain '-').
  add_executable(netproto_acceptance_tests
    tests/framing_test.cpp
    tests/buffer_limits_test.cpp
    tests/unknown_packet_test.cpp
    tests/handshake_test.cpp
    tests/account_data_test.cpp
    tests/inter_length_tables_test.cpp
    tests/stall_policy_test.cpp
    tests/flood_guard_test.cpp
    tests/link_session_test.cpp
    tests/rule_manifest_test.cpp
  )
  target_link_libraries(netproto_acceptance_tests PRIVATE rathena::netproto GTest::gtest_main)

  include(GoogleTest)
  gtest_discover_tests(netproto_acceptance_tests)
endif()

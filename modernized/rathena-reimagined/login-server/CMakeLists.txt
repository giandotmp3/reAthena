# rathena-login-server: C++20 rebuild of the rAthena login server.
#
# Scope (REIMAGINED_ARCHITECTURE.md §0.1, §3 login row, §10 unit `login-server`):
#   - owns BR-LOGIN-001..039 (BR-LOGIN-004 removed at HITL #1; BR-LOGIN-003 MD5
#     challenge shim is opt-in, default OFF) and BR-INTER-004, 005, 006, 014, 017
#   - domain: Account, LoginTicket (30 s TTL), OnlineState, IpBan, WebAuthToken
#   - password verification through a PasswordHasher interface (Argon2id; libsodium
#     binding is a stub until the dependency is vendored) on a bounded worker pool
#     with queue-depth back-pressure (queue full -> refuse code 3)
#   - pure decision functions for auth order and refusal codes
#   - client protocol stubs: CA_LOGIN*, CA_REQ_HASH, AC_ACCEPT_LOGIN, AC_REFUSE_LOGIN
#
# Self-contained: no dependency on sibling units (core/netproto/persistence).
# Small local copies of the needed types live here until the unit moves into src/.
#
# Build:  cmake -B build && cmake --build build && ctest --test-dir build
# Offline: -DFETCHCONTENT_SOURCE_DIR_GOOGLETEST=/path/to/googletest
cmake_minimum_required(VERSION 3.20)
project(rathena_login_server VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(RATHENA_LOGIN_BUILD_TESTS "Build login-server acceptance tests" ON)
option(RATHENA_LOGIN_WERROR "Treat warnings as errors" OFF)

find_package(Threads REQUIRED)

add_library(rathena_login
  src/md5.cpp
  src/rng.cpp
  src/config.cpp
  src/auth_rules.cpp
  src/account_admin.cpp
  src/ip_ban.cpp
  src/ticket_store.cpp
  src/online_state.cpp
  src/char_server_registry.cpp
  src/web_token.cpp
  src/password.cpp
  src/login_log.cpp
  src/protocol/client_packets.cpp
  src/protocol/client_handler.cpp
)
add_library(rathena::login ALIAS rathena_login)
target_include_directories(rathena_login PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)
target_compile_features(rathena_login PUBLIC cxx_std_20)
target_link_libraries(rathena_login PUBLIC Threads::Threads)

add_executable(login-server app/main.cpp)
target_link_libraries(login-server PRIVATE rathena::login)

foreach(tgt rathena_login login-server)
  if(MSVC)
    target_compile_options(${tgt} PRIVATE /W4 /permissive-)
    if(RATHENA_LOGIN_WERROR)
      target_compile_options(${tgt} PRIVATE /WX)
    endif()
  else()
    target_compile_options(${tgt} PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
    if(RATHENA_LOGIN_WERROR)
      target_compile_options(${tgt} PRIVATE -Werror)
    endif()
  endif()
endforeach()

if(RATHENA_LOGIN_BUILD_TESTS)
  enable_testing()
  include(FetchContent)
  # GoogleTest v1.15.2 (pinned to the release tag's commit).
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

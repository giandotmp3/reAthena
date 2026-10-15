# rathena-login-server: C++20 rebuild of the rAthena login server.
#
# Scope (REIMAGINED_ARCHITECTURE.md §0.1, §3 login row, §10 unit `login-server`):
#   - owns BR-LOGIN-001..039 (BR-LOGIN-004 removed at HITL #1; BR-LOGIN-003 MD5
#     challenge shim is opt-in, default OFF) and BR-INTER-004, 005, 006, 014, 017
#   - domain: Account, LoginTicket (30 s TTL), OnlineState, IpBan, WebAuthToken
#   - password verification through a PasswordHasher interface (Argon2id via the
#     P-H-C reference implementation, PHC strings compatible with libsodium's
#     crypto_pwhash_str) on a bounded worker pool with queue-depth back-pressure
#     (queue full -> refuse code 3)
#   - pure decision functions for auth order and refusal codes
#   - client protocol stubs: CA_LOGIN*, CA_REQ_HASH, AC_ACCEPT_LOGIN, AC_REFUSE_LOGIN
#
# Self-contained: no dependency on sibling units (core/netproto/persistence).
# Small local copies of the needed types live here until the unit moves into src/.
#
# Build:  cmake -B build && cmake --build build && ctest --test-dir build
# Offline: -DFETCHCONTENT_SOURCE_DIR_GOOGLETEST=/path/to/googletest
#          -DFETCHCONTENT_SOURCE_DIR_ARGON2=/path/to/phc-winner-argon2 (tag 20190702)
cmake_minimum_required(VERSION 3.20)
project(rathena_login_server VERSION 0.1.0 LANGUAGES C CXX) # C: the Argon2 reference code

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(RATHENA_LOGIN_BUILD_TESTS "Build login-server acceptance tests" ON)
option(RATHENA_LOGIN_WERROR "Treat warnings as errors" OFF)

find_package(Threads REQUIRED)
include(FetchContent)

# Argon2 reference implementation (github.com/P-H-C/phc-winner-argon2, CC0 / Apache-2.0),
# release 20190702 = commit 62358ba2123abd17fccf2a108a301d4b52c01a7c. The SHA-256 is the
# one Homebrew pins for this exact URL, and it was reproduced from a git clone of the tag
# (`git archive --prefix=phc-winner-argon2-20190702/ 20190702 | gzip -n -6`).
# Built as a small static C library with its own default flags (our warning flags apply
# to our targets only; the header is a SYSTEM include). ARGON2_NO_THREADS: the lanes run
# sequentially, concurrency comes from HashWorkerPool. ref.c (portable) rather than the
# SSE opt.c, so one code path serves x86-64 and arm64.
FetchContent_Declare(argon2
  URL      https://github.com/P-H-C/phc-winner-argon2/archive/refs/tags/20190702.tar.gz
  URL_HASH SHA256=daf972a89577f8772602bf2eb38b6a3dd3d922bf5724d45e7f9589b5e830442c
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(argon2) # upstream ships a Makefile only: this just populates
if(NOT TARGET rathena_argon2)
  add_library(rathena_argon2 STATIC
    ${argon2_SOURCE_DIR}/src/argon2.c
    ${argon2_SOURCE_DIR}/src/core.c
    ${argon2_SOURCE_DIR}/src/encoding.c
    ${argon2_SOURCE_DIR}/src/ref.c
    ${argon2_SOURCE_DIR}/src/blake2/blake2b.c)
  target_include_directories(rathena_argon2 SYSTEM PUBLIC ${argon2_SOURCE_DIR}/include)
  target_include_directories(rathena_argon2 PRIVATE ${argon2_SOURCE_DIR}/src)
  target_compile_definitions(rathena_argon2 PUBLIC ARGON2_NO_THREADS)
  set_target_properties(rathena_argon2 PROPERTIES POSITION_INDEPENDENT_CODE ON C_STANDARD 99 C_EXTENSIONS ON)
endif()

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
  src/argon2id.cpp
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
target_link_libraries(rathena_login PUBLIC Threads::Threads PRIVATE rathena_argon2)
if(WIN32)
  target_link_libraries(rathena_login PRIVATE bcrypt) # BCryptGenRandom (os_random_bytes)
endif()

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

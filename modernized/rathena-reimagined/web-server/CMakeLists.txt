# rathena-web-server: C++20 rebuild of the rAthena web companion API.
#
# Scope (REIMAGINED_ARCHITECTURE.md §0.1, §3 web-server row, §10 unit `web-server`;
# AI_NATIVE_SPEC.md CAP-22, §3.4, §5.3):
#   - owns BR-WEB-001..017 (BR-WEB-010 INVERTED at HITL #1: every */load route
#     requires WebToken auth; unknown account -> no default row inserted)
#   - writes UserConfig, CharConfig, MerchantConfig, GuildEmblem, PartyBooking
#   - reads Account web token, Character, Guild, Party via read-only repositories
#   - pure rule functions (token check, guild-master check, emblem validation,
#     config merges, party booking validation/search, CORS exact-origin,
#     log redaction) live in rathena_web_rules; route handlers in rathena_web
#   - JSON only through nlohmann/json (S3); tokens never logged (S2 / BR-WEB-017)
#
# Self-contained: no dependency on sibling units. Repositories are in-memory
# until the persistence unit provides MySQL implementations; no live DB needed.
#
# Build:  cmake -B build && cmake --build build && ctest --test-dir build
# Offline: -DFETCHCONTENT_SOURCE_DIR_GOOGLETEST=/path -DFETCHCONTENT_SOURCE_DIR_NLOHMANN_JSON=/path
cmake_minimum_required(VERSION 3.20)
project(rathena_web_server VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(RATHENA_WEB_BUILD_TESTS "Build web-server acceptance tests" ON)
option(RATHENA_WEB_WERROR "Treat warnings as errors" OFF)
option(RATHENA_WEB_WITH_HTTPLIB "Build the cpp-httplib transport adapter" OFF)

include(FetchContent)

# nlohmann/json v3.11.3 (pinned to the release tag's commit). S3: the only JSON producer.
FetchContent_Declare(nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG        9cca280a4d0ccf0c08f47a99aa71d1b0e52f8d03 # v3.11.3
  GIT_SHALLOW    FALSE
)
set(JSON_BuildTests OFF CACHE INTERNAL "")
set(JSON_Install OFF CACHE INTERNAL "")
FetchContent_MakeAvailable(nlohmann_json)

find_package(Threads REQUIRED)

# Pure, IO-free rules (architecture §2 dependency rule: rules -> domain only).
add_library(rathena_web_rules
  src/auth_rules.cpp
  src/emblem_rules.cpp
  src/config_merge.cpp
  src/party_booking_rules.cpp
  src/cors.cpp
  src/log_redaction.cpp
)
add_library(rathena::web_rules ALIAS rathena_web_rules)
target_include_directories(rathena_web_rules PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)
target_compile_features(rathena_web_rules PUBLIC cxx_std_20)
target_link_libraries(rathena_web_rules PUBLIC nlohmann_json::nlohmann_json)

# Handlers, router, repositories, config.
add_library(rathena_web
  src/config.cpp
  src/http.cpp
  src/in_memory_repositories.cpp
  src/controllers.cpp
  src/router.cpp
)
add_library(rathena::web ALIAS rathena_web)
target_link_libraries(rathena_web PUBLIC rathena::web_rules Threads::Threads)

add_executable(web-server app/main.cpp)
target_link_libraries(web-server PRIVATE rathena::web)

set(_web_targets rathena_web_rules rathena_web web-server)

if(RATHENA_WEB_WITH_HTTPLIB)
  # cpp-httplib v0.18.3 (pinned). Transport only; all behavior lives in Router.
  FetchContent_Declare(httplib
    GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
    GIT_TAG        a7bc00e3307fecdb4d67545e93be7b88cfb1e186 # v0.18.3
    GIT_SHALLOW    FALSE
  )
  set(HTTPLIB_REQUIRE_OPENSSL OFF CACHE BOOL "" FORCE)
  set(HTTPLIB_USE_OPENSSL_IF_AVAILABLE OFF CACHE BOOL "" FORCE)
  set(HTTPLIB_USE_ZLIB_IF_AVAILABLE OFF CACHE BOOL "" FORCE)
  set(HTTPLIB_USE_BROTLI_IF_AVAILABLE OFF CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(httplib)
  target_sources(web-server PRIVATE src/httplib_transport.cpp)
  target_compile_definitions(web-server PRIVATE RATHENA_WEB_WITH_HTTPLIB=1)
  target_link_libraries(web-server PRIVATE httplib::httplib)
endif()

foreach(tgt IN LISTS _web_targets)
  if(MSVC)
    target_compile_options(${tgt} PRIVATE /W4 /permissive-)
    if(RATHENA_WEB_WERROR)
      target_compile_options(${tgt} PRIVATE /WX)
    endif()
  else()
    target_compile_options(${tgt} PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
    if(RATHENA_WEB_WERROR)
      target_compile_options(${tgt} PRIVATE -Werror)
    endif()
  endif()
endforeach()

if(RATHENA_WEB_BUILD_TESTS)
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

cmake_minimum_required(VERSION 3.20)
project(script_host VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# The scaffold builds without Luau so domain/contract tests run anywhere.
# Turn this on once the Luau VM is vendored (M0 spike c decides allocator-tag
# vs per-mod lua_State accounting; see REIMAGINED_ARCHITECTURE section 5).
option(SCRIPT_HOST_WITH_LUAU "Link the Luau VM" OFF)
option(SCRIPT_HOST_BUILD_TESTS "Build acceptance tests" ON)

add_library(script_host
  src/ids.cpp
  src/event_bus.cpp
  src/effect_queue.cpp
  src/budget.cpp
  src/sandbox.cpp
  src/dialogue.cpp
  src/reload.cpp
  src/storage.cpp
  src/numeric.cpp
  src/api_schema.cpp)
target_include_directories(script_host PUBLIC include)
target_compile_features(script_host PUBLIC cxx_std_20)
if(MSVC)
  target_compile_options(script_host PRIVATE /W4 /WX)
else()
  target_compile_options(script_host PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

if(SCRIPT_HOST_WITH_LUAU)
  find_package(Luau CONFIG REQUIRED)
  target_link_libraries(script_host PUBLIC Luau::Luau.VM Luau::Luau.Compiler)
  target_compile_definitions(script_host PUBLIC SCRIPT_HOST_WITH_LUAU=1)
endif()

if(SCRIPT_HOST_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

cmake_minimum_required(VERSION 3.20)
project(gateway VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(GATEWAY_SANITIZE "Build with ASan/UBSan" OFF)

add_library(gateway STATIC src/rules.cpp src/inproc.cpp src/misc.cpp src/services.cpp)
target_include_directories(gateway PUBLIC include)
target_compile_features(gateway PUBLIC cxx_std_20)
target_compile_options(gateway PRIVATE -Wall -Wextra -Wpedantic)
if(GATEWAY_SANITIZE)
  target_compile_options(gateway PUBLIC -fsanitize=address,undefined)
  target_link_options(gateway PUBLIC -fsanitize=address,undefined)
endif()

add_executable(rathena_gateway src/main.cpp)
target_link_libraries(rathena_gateway PRIVATE gateway)

enable_testing()
add_executable(gateway_tests
  tests/main.cpp tests/test_pure.cpp tests/test_login.cpp tests/test_char.cpp tests/test_security.cpp)
target_link_libraries(gateway_tests PRIVATE gateway)
target_compile_definitions(gateway_tests PRIVATE GATEWAY_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
add_test(NAME acceptance COMMAND gateway_tests)

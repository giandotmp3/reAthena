# Replays the status golden inside script-host's REAL mod sandbox (same module system as the mods loader), to show that the status layer needs no change to
# script-host. Not part of any build; run by hand:
#   RATHENA_LUAU_ROOT=<luau src> RATHENA_LUAU_LIB_DIR=<luau build> cmake -S tools/host_status -B $SCRATCH/host-status -G Ninja -DCMAKE_BUILD_TYPE=Release
#   cmake --build $SCRATCH/host-status && $SCRATCH/host-status/host_status .     (after tools/run_tests.py has expanded build/status_golden_re.luau)
cmake_minimum_required(VERSION 3.20)
project(host_status CXX)
set(CMAKE_CXX_STANDARD 20)
set(SCRIPT_HOST_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../../script-host script_host_build)
add_executable(host_status main.cpp)
target_link_libraries(host_status script_host)

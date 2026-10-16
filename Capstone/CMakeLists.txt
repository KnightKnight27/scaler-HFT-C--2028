cmake_minimum_required(VERSION 3.16)
project(OrderBook CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# CMake's default Release flags are "-O3 -DNDEBUG". The assignment requires -O2,
# so override them. NDEBUG strips the debug-only invariant checks from timed builds.
set(CMAKE_CXX_FLAGS_RELEASE "-O2 -DNDEBUG")
set(CMAKE_CXX_FLAGS_DEBUG "-O0 -g")

option(OB_PERF "Add -g -fno-omit-frame-pointer for perf call graphs (still -O2)" OFF)
option(OB_SANITIZE "Build with -fsanitize=address,undefined (verification only, never timing)" OFF)

add_executable(bench src/bench.cpp src/workload.cpp src/verify.cpp)
target_include_directories(bench PRIVATE include src)
target_compile_options(bench PRIVATE -Wall -Wextra -Wpedantic)

# Edge tests always keep assert() enabled, even in Release.
add_executable(edge_tests tests/edge_tests.cpp)
target_include_directories(edge_tests PRIVATE include src)
target_compile_options(edge_tests PRIVATE -Wall -Wextra -Wpedantic -UNDEBUG)

if(OB_PERF)
    target_compile_options(bench PRIVATE -g -fno-omit-frame-pointer)
endif()

if(OB_SANITIZE)
    target_compile_options(bench PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer -g)
    target_link_options(bench PRIVATE -fsanitize=address,undefined)
endif()

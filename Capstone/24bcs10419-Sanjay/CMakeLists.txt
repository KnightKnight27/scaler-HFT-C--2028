cmake_minimum_required(VERSION 3.14)
project(OrderBookBenchmark LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---- Release flags as required by the assignment (-O2) ----
set(CMAKE_CXX_FLAGS_RELEASE "-O2")

# Default to Release if no build type specified
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# ---- Include paths ----
include_directories(${CMAKE_SOURCE_DIR}/include)

# ---- Benchmark executable ----
add_executable(bench src/bench.cpp)

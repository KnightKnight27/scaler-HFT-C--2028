cmake_minimum_required(VERSION 3.14)
project(LowLatencyOrderBook LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS_RELEASE "-O2 -Wall -Wextra -Wpedantic")

include_directories(include)

set(COMMON_SOURCES
    src/OrderBookA.cpp
    src/OrderBookB.cpp
    src/OrderBookC.cpp
)

add_executable(bench ${COMMON_SOURCES} src/bench.cpp)
add_executable(test_suite ${COMMON_SOURCES} src/test.cpp)

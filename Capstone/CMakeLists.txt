cmake_minimum_required(VERSION 3.14)
project(LowLatencyOrderBook LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Enforce -O2 compilation as required by the assignment
add_compile_options(-O2 -Wall -Wextra -Wpedantic)

include_directories(include)

set(SOURCES
    src/OrderBookA.cpp
    src/OrderBookB.cpp
    src/OrderBookC.cpp
    src/bench.cpp
)

add_executable(bench ${SOURCES})

# Test executables
add_executable(unit_tests src/OrderBookA.cpp src/OrderBookB.cpp src/OrderBookC.cpp src/unit_tests.cpp)
add_executable(stress_test src/OrderBookA.cpp src/OrderBookB.cpp src/OrderBookC.cpp src/stress_test.cpp)

enable_testing()
add_test(NAME VerifyTest COMMAND bench --verify)
add_test(NAME UnitTests COMMAND unit_tests)
add_test(NAME StressTest COMMAND stress_test)

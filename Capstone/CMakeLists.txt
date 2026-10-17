cmake_minimum_required(VERSION 3.16)

project(OrderBook LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_compile_options(-O2 -Wall -Wextra -Wpedantic)

include_directories(${CMAKE_CURRENT_SOURCE_DIR})

# Implementation libraries
add_library(orderbook_a STATIC impl_a/orderbook_a.cpp)
target_include_directories(orderbook_a PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/impl_a)

add_library(orderbook_b STATIC impl_b/orderbook_b.cpp)
target_include_directories(orderbook_b PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/impl_b)

add_library(orderbook_c STATIC impl_c/orderbook_c.cpp)
target_include_directories(orderbook_c PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/impl_c)

# Implementation A CMake target
add_executable(OrderBookA impl_a/main_a.cpp)
target_link_libraries(OrderBookA PRIVATE orderbook_a)

# Implementation B CMake target
add_executable(OrderBookB impl_b/main_b.cpp)
target_link_libraries(OrderBookB PRIVATE orderbook_b)

# Implementation C CMake target
add_executable(OrderBookC impl_c/main_c.cpp)
target_link_libraries(OrderBookC PRIVATE orderbook_c)

# Verification / Correctness test CMake target
add_executable(verify verify.cpp)
target_link_libraries(verify PRIVATE orderbook_a orderbook_b orderbook_c)

# Default OrderBook executable
add_executable(OrderBook main.cpp)
target_link_libraries(OrderBook PRIVATE orderbook_a)

# Benchmark program
add_executable(bench bench.cpp)
target_link_libraries(bench PRIVATE orderbook_a orderbook_b orderbook_c)

include(GNUInstallDirs)
install(TARGETS OrderBook OrderBookA OrderBookB OrderBookC verify bench
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

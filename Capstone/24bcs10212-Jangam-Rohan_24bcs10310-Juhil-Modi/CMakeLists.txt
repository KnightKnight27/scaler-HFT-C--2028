cmake_minimum_required(VERSION 3.16)

project(OrderBook LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(bench bench.cpp)
target_compile_options(bench PRIVATE -O2 -Wall -Wextra)

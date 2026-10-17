cmake_minimum_required(VERSION 3.10)
project(order_book CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# -O2 as required; -g and frame pointers make `perf record -g` call graphs readable.
set(CMAKE_CXX_FLAGS "-O2 -g -fno-omit-frame-pointer -Wall -Wextra")
add_executable(bench src/bench.cpp)

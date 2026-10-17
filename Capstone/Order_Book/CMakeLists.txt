cmake_minimum_required(VERSION 3.10)
project(order_book CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# -g and frame pointers are only there so perf record -g gives readable call graphs
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O2 -g -fno-omit-frame-pointer -Wall -Wextra")

add_executable(bench src/bench.cpp)
set_target_properties(bench PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR})

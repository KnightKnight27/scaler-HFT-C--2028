cmake_minimum_required(VERSION 3.10)
project(OrderBookCapstone CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# the pdf requires -O2
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O2 -Wall")

add_executable(bench
    bench.cpp
    impl_a_map.cpp
    impl_b_pqueue.cpp
    impl_c_array.cpp
)

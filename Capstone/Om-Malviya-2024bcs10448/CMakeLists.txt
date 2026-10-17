cmake_minimum_required(VERSION 3.16)
project(capstone_order_book CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# The assignment requires -O2.
set(CMAKE_CXX_FLAGS_RELEASE "-O2")

add_executable(bench bench.cpp)
# headers: common.h, impl_a_map.h, impl_b_hashmap.h, impl_c_array.h

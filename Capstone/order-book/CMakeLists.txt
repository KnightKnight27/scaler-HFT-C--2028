cmake_minimum_required(VERSION 3.14)
project(order_book CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
# -O2 as required by the spec (CMake's default Release flags use -O3).
set(CMAKE_CXX_FLAGS_RELEASE "-O2 -g -fno-omit-frame-pointer")

add_executable(bench src/bench.cpp)
target_compile_options(bench PRIVATE -Wall -Wextra -Wpedantic)

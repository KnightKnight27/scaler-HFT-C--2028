cmake_minimum_required(VERSION 3.13)  # `cmake -B build` needs 3.13
project(lob_capstone CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
set(CMAKE_CXX_FLAGS_RELEASE "-O2 -g -fno-omit-frame-pointer")
add_executable(bench src/bench.cpp)
target_include_directories(bench PRIVATE include src)
target_compile_options(bench PRIVATE -Wall -Wextra)

add_executable(test_orderbook tests/test_orderbook.cpp)
target_include_directories(test_orderbook PRIVATE include src)
target_compile_options(test_orderbook PRIVATE -Wall -Wextra)

enable_testing()
add_test(NAME unit_tests COMMAND test_orderbook)
add_test(NAME verify COMMAND bench --verify)

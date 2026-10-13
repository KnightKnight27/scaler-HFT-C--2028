cmake_minimum_required(VERSION 3.16)
project(order_book LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O2")

include_directories(include src bench)

add_executable(order_book_tests tests/order_book_contract.cpp)
add_executable(cross_implementation_tests tests/cross_implementation.cpp)
add_executable(bench bench/main.cpp)
enable_testing()
add_test(NAME order_book_contract COMMAND order_book_tests)
add_test(NAME cross_implementation COMMAND cross_implementation_tests)

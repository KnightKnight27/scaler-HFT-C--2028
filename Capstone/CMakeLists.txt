cmake_minimum_required(VERSION 3.25)

project(OrderBook LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -march=native -DNDEBUG")
add_compile_options(-Wall -Wextra -Wpedantic)

add_library(order_book src/order_book.cpp)
target_include_directories(order_book PUBLIC include)

add_executable(order_book_demo src/main.cpp)
target_link_libraries(order_book_demo PRIVATE order_book)

add_executable(order_book_bench bench/benchmark.cpp)
target_link_libraries(order_book_bench PRIVATE order_book)

enable_testing()
add_executable(order_book_tests tests/order_book_test.cpp)
target_link_libraries(order_book_tests PRIVATE order_book)
add_test(NAME order_book_tests COMMAND order_book_tests)

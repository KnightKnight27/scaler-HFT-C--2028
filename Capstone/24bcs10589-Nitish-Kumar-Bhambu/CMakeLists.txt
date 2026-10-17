cmake_minimum_required(VERSION 3.16)
project(low_latency_order_book LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(book
    order_book.cpp
    map_order_book.cpp
    ring_order_book.cpp
    bench.cpp
)

target_compile_options(book PRIVATE -O2 -g -fno-omit-frame-pointer -Wall -Wextra -Wpedantic)

enable_testing()
add_test(NAME verify COMMAND book --verify)

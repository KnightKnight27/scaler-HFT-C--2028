cmake_minimum_required(VERSION 3.16)
project(low_latency_order_book LANGUAGES CXX)

add_executable(
    bench
    order_book.cpp
    ordered_map_order_book.cpp
    sorted_vector_order_book.cpp
    fixed_array_order_book.cpp
    bench.cpp
)
target_compile_features(bench PRIVATE cxx_std_17)

if(MSVC)
    target_compile_options(bench PRIVATE /W4 /permissive-)
else()
    target_compile_options(bench PRIVATE -Wall -Wextra -Wpedantic)
endif()

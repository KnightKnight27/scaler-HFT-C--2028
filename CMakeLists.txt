cmake_minimum_required(VERSION 3.16)
project(order_book LANGUAGES CXX)
add_executable(bench bench.cpp workload.cpp verification.cpp
    implementation/map_order_book.cpp
    implementation/vector_order_book.cpp
    implementation/array_order_book.cpp)
target_compile_features(bench PRIVATE cxx_std_17)
set_target_properties(bench PROPERTIES CXX_EXTENSIONS OFF)
target_compile_options(bench PRIVATE -O2 -g -fno-omit-frame-pointer -Wall -Wextra -Wpedantic)
option(ENABLE_SANITIZERS "Enable address and undefined-behavior sanitizers" OFF)
if(ENABLE_SANITIZERS)
    target_compile_options(bench PRIVATE -fsanitize=address,undefined)
    target_link_options(bench PRIVATE -fsanitize=address,undefined)
endif()
enable_testing()
add_test(NAME correctness COMMAND bench --verify)
add_test(NAME correctness_seed_0 COMMAND bench --verify --ops 3000 --seed 0)
add_test(NAME correctness_seed_2026 COMMAND bench --verify --ops 3000 --seed 2026)

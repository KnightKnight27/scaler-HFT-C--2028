cmake_minimum_required(VERSION 3.16)
project(OrderBookCapstone CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Optimisation flags
if(MSVC)
    add_compile_options(/O2 /W4)
else()
    add_compile_options(-O2 -Wall -Wextra -Wpedantic)
endif()

# ---------------------------------------------------------------------------
# Shared library for all three implementations
# ---------------------------------------------------------------------------
add_library(order_book_lib STATIC
    src/map_order_book.cpp
    src/vector_order_book.cpp
    src/array_order_book.cpp
)
target_include_directories(order_book_lib PUBLIC include)

# ---------------------------------------------------------------------------
# Demo executable
# ---------------------------------------------------------------------------
add_executable(demo src/main.cpp)
target_link_libraries(demo PRIVATE order_book_lib)

# ---------------------------------------------------------------------------
# Correctness tests
# ---------------------------------------------------------------------------
add_executable(correctness_tests tests/correctness_tests.cpp)
target_include_directories(correctness_tests PRIVATE include)
target_link_libraries(correctness_tests PRIVATE order_book_lib)

# ---------------------------------------------------------------------------
# Benchmark
# ---------------------------------------------------------------------------
add_executable(bench bench.cpp)
target_include_directories(bench PRIVATE include)
target_link_libraries(bench PRIVATE order_book_lib)

# ---------------------------------------------------------------------------
# Optional: enable CTest
# ---------------------------------------------------------------------------
enable_testing()
add_test(NAME correctness COMMAND correctness_tests)

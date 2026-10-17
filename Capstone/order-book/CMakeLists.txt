cmake_minimum_required(VERSION 3.10)
project(order_book CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(bench
    src/bench.cpp
    src/verify.cpp
    src/workload.cpp
    src/platform.cpp
    src/map_order_book.cpp
    src/sorted_vector_order_book.cpp
    src/array_order_book.cpp
)

# -O2 as required by the brief. -g and -fno-omit-frame-pointer do not change the
# optimisation level; they give perf symbols and call stacks.
target_compile_options(bench PRIVATE -O2 -g -fno-omit-frame-pointer -Wall -Wextra)

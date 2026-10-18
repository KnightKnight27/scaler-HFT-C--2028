cmake_minimum_required(VERSION 3.10)
project(order_book_capstone CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# -O2 as required. -g and -fno-omit-frame-pointer only add symbols / keep the
# frame-pointer register so that `perf record -g` produces usable call graphs.
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O2 -g -fno-omit-frame-pointer -Wall -Wextra")

add_executable(bench
    src/bench.cpp
    src/verify.cpp
    src/workload.cpp
    src/book_map.cpp
    src/book_vector.cpp
    src/book_array.cpp)

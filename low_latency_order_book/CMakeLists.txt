cmake_minimum_required(VERSION 3.12)
project(LowLatencyOrderBook)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Compile with -O2 optimization as required by the assignment
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O2 -Wall -Wextra")

include_directories(include)

add_executable(bench 
    src/main.cpp 
    src/order_book_a.cpp
    src/order_book_b.cpp
    src/order_book_c.cpp
)
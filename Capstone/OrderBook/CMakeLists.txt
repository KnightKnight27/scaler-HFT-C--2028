cmake_minimum_required(VERSION 3.14)
project(OrderBookCapstone CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O2 -Wall -Wextra")

include_directories(include)

add_executable(bench
    src/orderBookMap.cpp
    src/orderBookHeap.cpp
    src/orderBookArray.cpp
    src/main.cpp
)

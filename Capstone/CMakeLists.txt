cmake_minimum_required(VERSION 3.15)
project(LowLatencyOrderBook LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    add_compile_options(/O2 /W4)
else()
    add_compile_options(-O2 -Wall -Wextra -pedantic)
endif()

add_executable(bench bench.cpp)

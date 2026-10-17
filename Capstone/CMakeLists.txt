cmake_minimum_required(VERSION 3.12)
project(LowLatencyOrderBook LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Optimization flag as specified in project requirements
set(CMAKE_CXX_FLAGS_RELEASE "-O2 -Wall -Wextra")
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

include_directories(include)

add_executable(bench src/main.cpp)

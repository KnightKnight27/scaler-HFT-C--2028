cmake_minimum_required(VERSION 3.16)
project(spsc_queue_benchmark LANGUAGES CXX)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)

add_executable(spsc_benchmark
    Lec-11/assignment.cpp/main.cpp
    Lec-11/assignment.cpp/spsc_queue.cpp
)
target_link_libraries(spsc_benchmark PRIVATE Threads::Threads)

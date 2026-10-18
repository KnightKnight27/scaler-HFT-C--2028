cmake_minimum_required(VERSION 3.16)

project(SPSCQueue LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Benchmark numbers only mean something with optimisation on.
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()
set(CMAKE_CXX_FLAGS_RELEASE "-O2")

# cmake -DSPSC_TSAN=ON  -> build with ThreadSanitizer to check for data races
option(SPSC_TSAN "Build with -fsanitize=thread" OFF)
if(SPSC_TSAN)
    add_compile_options(-fsanitize=thread -g)
    add_link_options(-fsanitize=thread)
endif()

find_package(Threads REQUIRED)

add_executable(SPSCQueue main.cpp spscqueue.h)
target_compile_options(SPSCQueue PRIVATE -Wall -Wextra)
target_link_libraries(SPSCQueue PRIVATE Threads::Threads)

add_executable(spsc_tests tests.cpp spscqueue.h)
target_compile_options(spsc_tests PRIVATE -Wall -Wextra)
target_link_libraries(spsc_tests PRIVATE Threads::Threads)

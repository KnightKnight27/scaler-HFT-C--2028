cmake_minimum_required(VERSION 3.16)

project(SPSCQueue LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)

add_executable(SPSCQueue main.cpp)
target_link_libraries(SPSCQueue PRIVATE Threads::Threads)

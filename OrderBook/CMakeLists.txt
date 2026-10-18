cmake_minimum_required(VERSION 3.16)

project(OrderBook LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    add_compile_options(/O2)
else()
    add_compile_options(-O2 -Wall -Wextra)
endif()

include_directories(${CMAKE_CURRENT_SOURCE_DIR})

add_library(implA STATIC impl_a/orderbook_a.cpp)
target_include_directories(implA PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_library(implB STATIC impl_b/orderbook_b.cpp)
target_include_directories(implB PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_library(implC STATIC impl_c/orderbook_c.cpp)
target_include_directories(implC PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(bench bench.cpp)
target_link_libraries(bench implA implB implC)

add_executable(verify verify.cpp)
target_link_libraries(verify implA implB implC)

add_executable(demo demo.cpp)
target_link_libraries(demo implA implB implC)

cmake_minimum_required(VERSION 3.16)
project(low_latency_orderbook LANGUAGES CXX)

add_library(orderbook INTERFACE)
target_include_directories(orderbook INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(orderbook INTERFACE cxx_std_17)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Override CMake's usual Release -O3 default for this assignment.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    set(CMAKE_CXX_FLAGS_RELEASE "-O2 -DNDEBUG")
else()
    message(FATAL_ERROR "Use GCC or Clang to satisfy the required -O2 build.")
endif()

add_library(orderbook_a STATIC impl_a/orderbook_a.cpp)
target_link_libraries(orderbook_a PUBLIC orderbook)
target_compile_options(orderbook_a PRIVATE -Wall -Wextra -Wpedantic)

add_library(orderbook_b STATIC impl_b/orderbook_b.cpp)
target_link_libraries(orderbook_b PUBLIC orderbook)
target_compile_options(orderbook_b PRIVATE -Wall -Wextra -Wpedantic)

add_library(orderbook_c STATIC impl_c/orderbook_c.cpp)
target_link_libraries(orderbook_c PUBLIC orderbook)
target_compile_options(orderbook_c PRIVATE -Wall -Wextra -Wpedantic)

add_executable(demo main.cpp)
target_link_libraries(demo PRIVATE orderbook_a orderbook_b orderbook_c)
target_compile_options(demo PRIVATE -Wall -Wextra -Wpedantic)

add_executable(bench bench.cpp)
target_link_libraries(bench PRIVATE orderbook_a orderbook_b orderbook_c)
target_compile_options(bench PRIVATE -Wall -Wextra -Wpedantic)

include(CTest)
if(BUILD_TESTING)
    add_executable(verify verify.cpp)
    target_link_libraries(verify PRIVATE orderbook_a orderbook_b orderbook_c)
    target_compile_options(verify PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME implementations_abc COMMAND verify)
endif()

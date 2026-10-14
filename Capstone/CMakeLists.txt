cmake_minimum_required(VERSION 3.16)
project(low_latency_order_book LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_FLAGS_RELEASE "-O2 -g -DNDEBUG -fno-omit-frame-pointer")

add_library(order_book
    src/implementation_a_map.cpp
    src/implementation_b_indexed.cpp
    src/implementation_c_array.cpp
    src/price_utils.cpp
)
target_include_directories(order_book PUBLIC include)
target_compile_options(order_book PRIVATE -Wall -Wextra -Wpedantic)

add_executable(bench src/benchmark.cpp)
target_link_libraries(bench PRIVATE order_book)
target_compile_options(bench PRIVATE -Wall -Wextra -Wpedantic)

add_executable(order_book_tests tests/order_book_tests.cpp)
target_link_libraries(order_book_tests PRIVATE order_book)
target_compile_options(order_book_tests PRIVATE -Wall -Wextra -Wpedantic)

enable_testing()
add_test(NAME order_book_tests COMMAND order_book_tests)
add_test(NAME implementation_agreement COMMAND bench --verify --ops 10000)

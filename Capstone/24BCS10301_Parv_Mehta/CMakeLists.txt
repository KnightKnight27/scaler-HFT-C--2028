cmake_minimum_required(VERSION 3.10)
project(orderbook_capstone CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()
# The assignment asks for -O2. RelWithDebInfo is -O2 -g.
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "-O2 -g")

set(BOOK_SOURCES src/map_book.cpp src/heap_book.cpp src/array_book.cpp)

add_executable(bench src/bench.cpp ${BOOK_SOURCES})
target_include_directories(bench PRIVATE include)
target_compile_options(bench PRIVATE -Wall -Wextra)

add_executable(tests src/tests.cpp ${BOOK_SOURCES})
target_include_directories(tests PRIVATE include)
target_compile_options(tests PRIVATE -Wall -Wextra)

enable_testing()
add_test(NAME unit_tests COMMAND tests)
add_test(NAME verify COMMAND bench --verify)

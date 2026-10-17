cmake_minimum_required(VERSION 3.14)
project(CapstoneOrderBook CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Default to -O2 -g so plain `cmake ..` matches the required -O2
# and perf record gets symbols.
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

add_executable(bench bench/bench.cpp)
target_include_directories(bench PRIVATE main_ob bench)
target_compile_options(bench PRIVATE -fno-omit-frame-pointer)

# Optional helper tests: only built if the files exist in the repo.
if(EXISTS ${CMAKE_SOURCE_DIR}/decision_tests/tick_test.cpp)
  add_executable(tick_test decision_tests/tick_test.cpp)
  target_include_directories(tick_test PRIVATE main_ob bench)
endif()

if(EXISTS ${CMAKE_SOURCE_DIR}/decision_tests/workload_test.cpp)
  add_executable(workload_test decision_tests/workload_test.cpp)
  target_include_directories(workload_test PRIVATE main_ob bench)
endif()
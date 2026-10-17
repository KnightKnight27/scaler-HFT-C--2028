cmake_minimum_required(VERSION 3.16)
project(OrderBook CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---- Build flags -----------------------------------------------------------
# Required: -O2
# Additional recommended flags for HFT-style code:
#   -march=native   — use all CPU instructions available on this machine
#   -funroll-loops  — unroll tight inner loops
#   -Wall -Wextra   — catch bugs

add_compile_options(
    -O2
    -march=native
    -funroll-loops
    -Wall
    -Wextra
)

# ---- Include directories ---------------------------------------------------
include_directories(include)

# ---- Benchmark binary ------------------------------------------------------
add_executable(bench src/bench.cpp)

# ---- Installation ----------------------------------------------------------
install(TARGETS bench DESTINATION bin)

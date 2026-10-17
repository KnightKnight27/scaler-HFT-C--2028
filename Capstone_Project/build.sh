#!/usr/bin/env bash
# build.sh — Quick build script for Linux/WSL
# Usage: bash build.sh
set -euo pipefail

CXX=${CXX:-g++}
BIN=bench

echo "[build] Using compiler: $CXX"

$CXX -std=c++17 -O2 -march=native -funroll-loops -Wall -Wextra \
    -I include \
    src/bench.cpp \
    -o "$BIN"

echo "[build] Done. Binary: ./$BIN"
echo ""
echo "Run correctness check:"
echo "  ./$BIN --verify"
echo ""
echo "Run benchmarks:"
echo "  ./$BIN --impl A"
echo "  ./$BIN --impl B"
echo "  ./$BIN --impl C"

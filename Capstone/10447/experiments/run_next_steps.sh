#!/bin/bash
# one extra bad-case check for BookC. leaves the normal bench alone.
set -u
cd "$(dirname "$0")/.."
mkdir -p results/current_machine
g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -o /tmp/orderbook-next-steps experiments/next_steps.cpp || exit 1
/tmp/orderbook-next-steps | tee results/current_machine/bookc_sparse_scan.txt

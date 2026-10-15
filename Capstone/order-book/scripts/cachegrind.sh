#!/usr/bin/env bash
# Simulated cache + branch-predictor counters via Valgrind cachegrind, for
# machines where perf cannot read hardware counters (macOS, Docker on Apple
# Silicon). Runs inside a Debian container; output in results/cachegrind/.
# These are SIMULATED numbers (Valgrind's model of an L1/LL cache and a simple
# branch predictor), useful for comparing A/B/C, not absolute hardware truth.
set -euo pipefail
cd "$(dirname "$0")/.."
docker run --rm -v "$PWD":/work -w /work debian:bookworm bash -c '
  set -euo pipefail
  apt-get update -qq >/dev/null && apt-get install -y -qq g++ valgrind >/dev/null 2>&1
  mkdir -p results/cachegrind
  g++ -std=c++17 -O2 -g -o /tmp/bench src/bench.cpp
  valgrind --version > results/cachegrind/env.txt
  for impl in A B C 0; do
    valgrind --tool=cachegrind --cache-sim=yes --branch-sim=yes \
      --cachegrind-out-file=/dev/null /tmp/bench --impl $impl --untimed --runs 1 \
      > /dev/null 2> results/cachegrind/cachegrind_$impl.txt
    echo "== $impl"; grep -E "I *refs|D *refs|D1 *misses|LLd misses|Mispred|Branches" results/cachegrind/cachegrind_$impl.txt
  done'

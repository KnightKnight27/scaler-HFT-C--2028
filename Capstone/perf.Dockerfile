# Rohan Ranjan-10428-Capstone project
# Linux + g++ + perf + valgrind, for running run_perf.sh and cachegrind.sh on a Mac
FROM debian:bookworm
RUN apt-get update -qq && apt-get install -y -qq g++ linux-perf valgrind >/dev/null && rm -rf /var/lib/apt/lists/*

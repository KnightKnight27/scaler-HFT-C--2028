# Linux environment for building the benchmark and running perf / cachegrind.
# I develop on a Mac, which has no perf, so all profiling is done inside this
# container. Build it from the project directory with:
#
#   docker build -t orderbook-perf -f perf/Dockerfile .
#
# and then use perf/run_perf.sh (see README).

FROM debian:bookworm

RUN apt-get update && \
    DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
        g++ make cmake linux-perf valgrind python3 ca-certificates && \
    rm -rf /var/lib/apt/lists/* && \
    perf --version && valgrind --version && g++ --version | head -1

WORKDIR /work

#!/usr/bin/env bash
# Runs scripts/perf_linux.sh inside an Ubuntu 24.04 container, for when no Linux
# machine is at hand (we used it on macOS with Docker Desktop):
#
#   scripts/perf_docker.sh [output-dir]       (default: results/linux-docker)
#
# Docker Desktop's VM has no hardware PMU, so perf only reports software
# counters (task-clock, page-faults) there. cachegrind still works and gives
# simulated cache and branch numbers.
set -euo pipefail

cd "$(dirname "$0")/.."
OUT=${1:-results/linux-docker}

# --privileged lets perf use the perf_event_open system call inside the container.
docker run --rm --privileged -v "$PWD":/work -w /work ubuntu:24.04 bash -c "
    set -e
    export DEBIAN_FRONTEND=noninteractive
    apt-get update -qq > /dev/null
    apt-get install -y -qq g++ valgrind python3 linux-tools-generic > /dev/null
    PERF=\$(ls /usr/lib/linux-tools/*/perf | head -1) scripts/perf_linux.sh '$OUT'
"

#!/usr/bin/env bash
# Runs perf/run_perf.sh inside a Linux container. This is how the perf data
# in perf/results/ was produced on my Mac. --privileged is needed so that perf
# is allowed to open perf events inside the container.
set -euo pipefail
cd "$(dirname "$0")/.."
docker build -q -t orderbook-perf -f perf/Dockerfile . > /dev/null
docker run --rm --privileged -v "$PWD":/work -w /work \
    -e OPS="${OPS:-1000000}" -e CACHEGRIND="${CACHEGRIND:-1}" \
    orderbook-perf bash perf/run_perf.sh

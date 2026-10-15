#!/usr/bin/env bash
# Runs scripts/run_perf.sh inside a privileged Debian container (for macOS hosts).
# Hardware counters are usually NOT exposed to the Docker VM on Apple Silicon,
# so only software counters (task-clock, page-faults) will have values.
set -euo pipefail
cd "$(dirname "$0")/.."
docker run --rm --privileged -v "$PWD":/work -w /work debian:bookworm bash -c '
  apt-get update -qq >/dev/null && apt-get install -y -qq g++ linux-perf >/dev/null 2>&1
  echo -1 > /proc/sys/kernel/perf_event_paranoid
  ./scripts/run_perf.sh'
rm -f bench  # Linux binary; rebuild natively afterwards

#pragma once

#include "workload.hpp"

// Runs the correctness tests and prints PASS or FAIL for each implementation:
//   1. Edge cases from the brief, and a few more, checked on each book.
//   2. A self-test of the hash table used by implementation C.
//   3. Random workloads (the benchmark workload and a churn workload) run
//      through A, B, C and a brute-force reference book in lockstep, comparing
//      every result.
// Returns true when every implementation passes.
bool run_verify(const WorkloadConfig& benchmark_config);

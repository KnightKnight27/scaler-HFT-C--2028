// verify.h — correctness check behind `bench --verify`.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

// Replays the generated workload through each implementation named in `impls`
// (any of "ABC"), in lockstep with the oracle. After EVERY op it compares BBO,
// get_bids(10), get_asks(10) and the result of every cancel. Every
// kDeepCheckInterval ops (and at the end) it also compares the full depth of both
// sides and runs the book's own check_invariants().
//
// On the first mismatch for an implementation, prints the op index, the op, and the
// expected vs actual values, then moves on to the next implementation.
// Returns 0 if every requested implementation passed, 1 otherwise.
int run_verify(const std::string& impls, std::size_t num_ops, uint32_t seed);

// Deterministic workload generation. The whole operation stream is generated up
// front (outside any timed region) so every implementation replays exactly the
// same operations.
#pragma once

#include <cstdint>
#include <random>
#include <string>
#include <vector>

#include "common.hpp"

enum class OpType : uint8_t { Add, Cancel, Bbo, Depth };
constexpr int kNumOpTypes = 4;

struct Op {
    OpType type;
    Side side;
    int depth;  // Depth ops: number of levels for get_bids/get_asks
    uint64_t id;
    double price;
    long qty;
};

struct Workload {
    std::string name;
    std::vector<Op> ops;
    size_t count[kNumOpTypes] = {};
    size_t peak_live = 0;  // most orders resting at once
};

// Benchmark workload from the spec: `n` core operations (~70% add, ~20% cancel of a
// random live order, ~10% BBO) plus a Depth op (get_bids(10) + get_asks(10)) after
// every 100 core operations. Prices are uniform over each side's band, qty 1..100.
Workload make_benchmark_workload(size_t n, uint32_t seed);

// Adversarial workload for --verify: random (non-monotonic) 64-bit ids, prices
// clustered around the inside so levels empty and the best price moves constantly,
// cancels of unknown / already-cancelled ids, invalid prices and quantities,
// duplicate ids, and random depths.
Workload make_stress_workload(size_t n, uint32_t seed);

// Uniform integer in [0, n) from a 32-bit Mersenne Twister draw. Done by hand
// (multiply-shift) because std::uniform_int_distribution's output differs between
// standard libraries, which would make the workload non-portable.
inline uint32_t uniform(std::mt19937& rng, uint32_t n) {
    return static_cast<uint32_t>((static_cast<uint64_t>(rng()) * n) >> 32);
}

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "common.hpp"

enum class OpType : std::uint8_t { Add, Cancel, Bbo };

// One pre-generated operation. Only Add uses side, qty and price.
struct Op {
    OpType type;
    Side side;
    std::uint32_t qty;
    std::uint64_t id;
    double price;
};

struct WorkloadConfig {
    std::size_t num_ops = 1000000;
    std::uint32_t seed = 42;
    int add_percent = 70;
    int cancel_percent = 20;         // the remaining percent are get_bbo() calls
    bool cancel_all_at_end = false;  // append one cancel for every order still resting
};

struct Workload {
    std::vector<Op> ops;
    std::size_t adds = 0;
    std::size_t cancels = 0;
    std::size_t bbo_queries = 0;
    std::size_t max_live_orders = 0;  // used as the books' capacity hint
};

// After every kDepthEvery operations, call get_bids(kDepth) and get_asks(kDepth).
constexpr int kDepthEvery = 100;
constexpr int kDepth = 10;

// Generates the whole workload up front, so the random number generator is not
// part of any timing and every book sees exactly the same operations.
//   side:   50% buy, 50% sell
//   qty:    uniform in 1..100
//   price:  uniform over the 5000 valid ticks of the order's side
//   cancel: a uniformly chosen order that is still live. If nothing is live,
//           the operation becomes an add instead.
Workload generate_workload(const WorkloadConfig& config);

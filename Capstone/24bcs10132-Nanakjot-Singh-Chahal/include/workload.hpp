// Deterministic workload shared by the benchmark and the correctness check.
#pragma once

#include <random>
#include <vector>

#include "common.hpp"

enum class OpType : uint8_t { Add, Cancel, Bbo };

struct Op {
    OpType type;
    Side side;
    uint64_t id;
    double price;
    long qty;
};

// ~70% add, ~20% cancel (always of a currently live order), ~10% BBO query.
// Generated up front so the RNG cost is never inside the timed region and
// every implementation sees exactly the same sequence.
inline std::vector<Op> make_workload(size_t n_ops, uint32_t seed = 42) {
    std::mt19937 rng(seed);
    std::uniform_int_distribution<int> pct(0, 99);
    std::uniform_int_distribution<int> coin(0, 1);
    std::uniform_int_distribution<int> bid_tick(ticks::kMinBid, ticks::kMaxBid);
    std::uniform_int_distribution<int> ask_tick(ticks::kMinAsk, ticks::kMaxAsk);
    std::uniform_int_distribution<long> qty(1, 100);

    std::vector<Op> ops;
    ops.reserve(n_ops);
    std::vector<uint64_t> live;  // ids that are resting right now
    live.reserve(n_ops);
    uint64_t next_id = 1;

    for (size_t i = 0; i < n_ops; ++i) {
        const int r = pct(rng);
        if (r < 70 || (r < 90 && live.empty())) {
            const Side s = coin(rng) ? Side::Buy : Side::Sell;
            const int t = s == Side::Buy ? bid_tick(rng) : ask_tick(rng);
            const uint64_t id = next_id++;
            ops.push_back({OpType::Add, s, id, ticks::to_price(t), qty(rng)});
            live.push_back(id);
        } else if (r < 90) {
            std::uniform_int_distribution<size_t> pick(0, live.size() - 1);
            const size_t k = pick(rng);
            ops.push_back({OpType::Cancel, Side::Buy, live[k], 0.0, 0});
            live[k] = live.back();  // O(1) swap-remove
            live.pop_back();
        } else {
            ops.push_back({OpType::Bbo, Side::Buy, 0, 0.0, 0});
        }
    }
    return ops;
}

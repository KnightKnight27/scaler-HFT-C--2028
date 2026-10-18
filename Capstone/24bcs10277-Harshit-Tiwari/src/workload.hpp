#pragma once

#include <random>
#include <vector>

#include "common.hpp"

enum class OpType { Add, Cancel, Bbo };

struct Op {
    OpType type;
    Side side;
    uint64_t id;
    double price;
    long qty;
};

struct WorkloadConfig {
    int num_ops = 1000000;
    uint32_t seed = 42;
    int add_pct = 70;
    int cancel_pct = 20;    // the rest are BBO queries
    int band_ticks = 5000;  // prices come from the band_ticks ticks nearest the mid
};

// Generated up front so every book sees the same sequence and it is not timed.
inline std::vector<Op> make_workload(const WorkloadConfig& cfg) {
    std::mt19937 rng(cfg.seed);
    std::uniform_int_distribution<int> pct(0, 99);
    std::uniform_int_distribution<int> coin(0, 1);
    std::uniform_int_distribution<int> offset(0, cfg.band_ticks - 1);
    std::uniform_int_distribution<long> qty(1, 100);

    std::vector<Op> ops;
    ops.reserve(cfg.num_ops);
    std::vector<uint64_t> live;
    uint64_t next_id = 1;

    for (int i = 0; i < cfg.num_ops; i++) {
        int r = pct(rng);
        if (r < cfg.add_pct || (r < cfg.add_pct + cfg.cancel_pct && live.empty())) {
            Side side = coin(rng) ? Side::Buy : Side::Sell;
            int tick = side == Side::Buy ? MAX_BID_TICK - offset(rng) : MIN_ASK_TICK + offset(rng);
            ops.push_back({OpType::Add, side, next_id, to_price(tick), qty(rng)});
            live.push_back(next_id++);
        } else if (r < cfg.add_pct + cfg.cancel_pct) {
            size_t k = std::uniform_int_distribution<size_t>(0, live.size() - 1)(rng);
            ops.push_back({OpType::Cancel, Side::Buy, live[k], 0.0, 0});
            live[k] = live.back();
            live.pop_back();
        } else {
            ops.push_back({OpType::Bbo, Side::Buy, 0, 0.0, 0});
        }
    }
    return ops;
}

// Deterministic random workload shared by the benchmark and the correctness test
#pragma once
#include <cstdint>
#include <random>
#include "common.hpp"

enum OpType { OP_ADD, OP_CANCEL, OP_BBO };
struct Op { OpType type; Side side; double price; long qty; uint64_t id; };

// 70% add, 20% cancel (always a live order), 10% BBO. Fixed seed => repeatable
inline std::vector<Op> make_workload(size_t n, unsigned seed) {
    std::mt19937 rng(seed);
    std::vector<Op> ops;
    std::vector<uint64_t> live;
    uint64_t next_id = 1;
    for (size_t i = 0; i < n; ++i) {
        unsigned r = rng() % 100;
        if (r < 70 || (r < 90 && live.empty())) {                       // ADD
            Op op{OP_ADD, (rng() & 1) ? Side::Buy : Side::Sell, 0, 1 + (long)(rng() % 100), next_id++};
            int tick = (op.side == Side::Buy) ? MIN_BID + (int)(rng() % 5000) : MIN_ASK + (int)(rng() % 5000);
            op.price = to_price(tick);
            live.push_back(op.id);
            ops.push_back(op);
        } else if (r < 90) {                                            // CANCEL a live order
            size_t k = rng() % live.size();
            ops.push_back(Op{OP_CANCEL, Side::Buy, 0, 0, live[k]});
            live[k] = live.back();
            live.pop_back();
        } else {                                                        // BBO query
            ops.push_back(Op{OP_BBO, Side::Buy, 0, 0, 0});
        }
    }
    return ops;
}
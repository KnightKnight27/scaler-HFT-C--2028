// Builds the random workload once, so every implementation (and every run)
// replays exactly the same list of operations.
#pragma once

#include <cstdint>
#include <random>
#include <vector>

#include "types.hpp"

enum class OpType : uint8_t { Add, Cancel, Bbo };

struct Op {
    uint64_t id;
    int tick;
    int qty;
    OpType type;
    Side side;
};

constexpr int kSeed = 42;
constexpr int kDefaultOps = 1000000;
constexpr int kDepthEvery = 100;  // get_bids(10) + get_asks(10) after every 100th op
constexpr int kDepth = 10;

inline std::vector<Op> make_workload(int num_ops, int seed = kSeed) {
    std::mt19937 rng(seed);
    std::vector<Op> ops;
    ops.reserve(num_ops);

    std::vector<uint64_t> live;  // ids that can still be cancelled
    uint64_t next_id = 1;

    for (int i = 0; i < num_ops; i++) {
        int dice = rng() % 100;
        Op op{};

        if (dice < 70 || (dice < 90 && live.empty())) {
            // add (also used when there is nothing to cancel yet)
            op.type = OpType::Add;
            op.id = next_id++;
            op.side = (rng() % 2 == 0) ? Side::Buy : Side::Sell;
            op.qty = 1 + rng() % 100;
            if (op.side == Side::Buy)
                op.tick = kMinBidTick + rng() % (kMaxBidTick - kMinBidTick + 1);
            else
                op.tick = kMinAskTick + rng() % (kMaxAskTick - kMinAskTick + 1);
            live.push_back(op.id);
        } else if (dice < 90) {
            // cancel a random live order: swap it with the last one and pop
            size_t pick = rng() % live.size();
            op.type = OpType::Cancel;
            op.id = live[pick];
            live[pick] = live.back();
            live.pop_back();
        } else {
            op.type = OpType::Bbo;
        }
        ops.push_back(op);
    }
    return ops;
}

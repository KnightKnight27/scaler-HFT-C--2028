// workload.h
// Builds the same list of 1,000,000 operations every time (seed 42).
#pragma once

#include <cstdint>
#include <random>
#include <vector>

#include "common.h"

enum class OpType { Add, Cancel, BBO };

struct Op {
    OpType   type;
    uint64_t id;     // used by Add and Cancel
    Side     side;   // used by Add
    int      tick;   // used by Add
    long     qty;    // used by Add
};

// The "every 100 ops, call get_bids(10) and get_asks(10)" rule is NOT stored
// in the list. The benchmark and the test do it themselves with: (i + 1) % 100 == 0

inline std::vector<Op> generate_workload(int num_ops = 1000000, unsigned seed = 42) {
    std::mt19937 rng(seed);
    std::uniform_int_distribution<int> percent(0, 99);
    std::uniform_int_distribution<int> qty_dist(1, 100);
    std::uniform_int_distribution<int> bid_dist(BID_MIN_TICK, BID_MAX_TICK);
    std::uniform_int_distribution<int> ask_dist(ASK_MIN_TICK, ASK_MAX_TICK);

    std::vector<Op> ops;
    ops.reserve(num_ops);

    std::vector<uint64_t> live_ids;   // orders that are currently in the book
    uint64_t next_id = 1;             // IDs go 1, 2, 3, ... and are never reused

    for (int i = 0; i < num_ops; ++i) {
        int roll = percent(rng);

        OpType type;
        if (roll < 70)      type = OpType::Add;
        else if (roll < 90) type = OpType::Cancel;
        else                type = OpType::BBO;

        // A cancel needs a live order. If there is none yet, make it an add instead.
        if (type == OpType::Cancel && live_ids.empty()) type = OpType::Add;

        Op op{};
        op.type = type;

        if (type == OpType::Add) {
            op.id   = next_id++;
            op.side = (percent(rng) < 50) ? Side::Buy : Side::Sell;
            op.tick = (op.side == Side::Buy) ? bid_dist(rng) : ask_dist(rng);
            op.qty  = qty_dist(rng);
            live_ids.push_back(op.id);
        } else if (type == OpType::Cancel) {
            // pick a random live order, then remove it from the live list
            std::uniform_int_distribution<size_t> pick(0, live_ids.size() - 1);
            size_t k = pick(rng);
            op.id = live_ids[k];
            live_ids[k] = live_ids.back();   // swap with the last one...
            live_ids.pop_back();             // ...and drop it (fast removal)
        }
        // BBO needs no extra fields

        ops.push_back(op);
    }
    return ops;
}

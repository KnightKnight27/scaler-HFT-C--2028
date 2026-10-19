// Random workload used by both --verify and --impl.  Generated fully up front
// so all three books get the same ops and the RNG is never inside a timing.
//
// Note: std::uniform_int_distribution gives different numbers on libstdc++
// and libc++ for the same engine, so I map mt19937 output to a range by hand.
// mt19937 itself is fully specified, so the workload is the same everywhere.
#pragma once

#include <cstdint>
#include <random>
#include <vector>

#include "common.hpp"

namespace ob {

enum class OpType : uint8_t { Add, Cancel, Bbo, Depth };

struct Op {
    OpType type;
    Side side;     // Add only
    uint64_t id;   // Add / Cancel
    int tick;      // Add only
    int qty;       // Add only
};

struct WorkloadStats {
    size_t adds = 0, cancels = 0, bbos = 0, depths = 0;
};

// Uniform int in [lo, hi].  Ranges here are tiny vs 2^32 so modulo bias
// doesn't matter.
inline int uniform(std::mt19937& rng, int lo, int hi) {
    return lo + static_cast<int>(rng() % static_cast<uint32_t>(hi - lo + 1));
}

// n_ops ops (70% add, 20% cancel, 10% bbo) plus a Depth op after every 100.
// Cancels always pick a live order; a cancel drawn on an empty book becomes
// an add.
inline std::vector<Op> make_workload(size_t n_ops, uint32_t seed, WorkloadStats* stats = nullptr) {
    std::mt19937 rng(seed);

    std::vector<Op> ops;
    ops.reserve(n_ops + n_ops / 100 + 1);

    // live set for picking cancels (ids are 1..N so a vector works as the index)
    std::vector<uint64_t> live;             // ids currently resting
    std::vector<uint32_t> pos_of_id(1, 0);  // id -> index in `live`
    uint64_t next_id = 1;
    WorkloadStats st;

    for (size_t i = 0; i < n_ops; ++i) {
        int r = uniform(rng, 0, 99);
        if (r >= 70 && r < 90 && live.empty()) r = 0;  // cancel with empty book -> add

        if (r < 70) {
            Op op{};
            op.type = OpType::Add;
            op.side = uniform(rng, 0, 1) == 0 ? Side::Buy : Side::Sell;
            op.id = next_id++;
            op.tick = (op.side == Side::Buy) ? uniform(rng, kBidMinTick, kBidMaxTick)
                                             : uniform(rng, kAskMinTick, kAskMaxTick);
            op.qty = uniform(rng, 1, 100);
            pos_of_id.push_back(static_cast<uint32_t>(live.size()));
            live.push_back(op.id);
            ops.push_back(op);
            ++st.adds;
        } else if (r < 90) {
            const size_t k = static_cast<size_t>(uniform(rng, 0, static_cast<int>(live.size()) - 1));
            const uint64_t id = live[k];
            // swap-remove from the live set
            live[k] = live.back();
            pos_of_id[live[k]] = static_cast<uint32_t>(k);
            live.pop_back();
            Op op{};
            op.type = OpType::Cancel;
            op.id = id;
            ops.push_back(op);
            ++st.cancels;
        } else {
            Op op{};
            op.type = OpType::Bbo;
            ops.push_back(op);
            ++st.bbos;
        }

        if ((i + 1) % 100 == 0) {
            Op op{};
            op.type = OpType::Depth;
            ops.push_back(op);
            ++st.depths;
        }
    }
    if (stats) *stats = st;
    return ops;
}

}  // namespace ob

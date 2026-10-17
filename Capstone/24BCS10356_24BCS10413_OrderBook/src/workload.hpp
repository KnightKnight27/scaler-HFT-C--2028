// Deterministic workload generation. The whole op stream is generated up front
// (including which live order each cancel targets) so that RNG cost and the
// live-order bookkeeping are never inside the timed region.
#pragma once

#include <cstdint>
#include <random>
#include <vector>

#include "order_book_common.hpp"

enum class OpType : uint8_t { Add, Cancel, Bbo, Depth };

struct Op {
    OpType type;
    Side side;      // Add only
    int32_t tick;   // Add only
    int32_t qty;    // Add only
    uint64_t id;    // Add: new id, Cancel: id to cancel
};

struct Workload {
    std::vector<Op> ops;
    size_t n_add = 0, n_cancel = 0, n_bbo = 0, n_depth = 0;
};

// Distribution: 70% add / 20% cancel / 10% bbo over `n_ops` random operations,
// plus one Depth op (get_bids(10) + get_asks(10)) after every 100 of them.
// Cancels always target a currently-live order; if none is live the slot
// becomes an add instead.
inline Workload make_workload(size_t n_ops, uint32_t seed) {
    std::mt19937 rng(seed);
    std::uniform_int_distribution<int> pct(0, 99);
    std::uniform_int_distribution<int> coin(0, 1);
    std::uniform_int_distribution<int> qty_d(1, 100);
    std::uniform_int_distribution<int> bid_d(kBidMinTick, kBidMaxTick);
    std::uniform_int_distribution<int> ask_d(kAskMinTick, kAskMaxTick);

    Workload w;
    w.ops.reserve(n_ops + n_ops / 100 + 1);

    std::vector<uint64_t> live;  // ids of currently-live orders (order irrelevant)
    live.reserve(n_ops);
    uint64_t next_id = 1;

    for (size_t i = 0; i < n_ops; ++i) {
        const int r = pct(rng);
        if (r < 70 || (r < 90 && live.empty())) {
            Op op{};
            op.type = OpType::Add;
            op.side = coin(rng) == 0 ? Side::Buy : Side::Sell;
            op.tick = op.side == Side::Buy ? bid_d(rng) : ask_d(rng);
            op.qty = qty_d(rng);
            op.id = next_id++;
            live.push_back(op.id);
            w.ops.push_back(op);
            ++w.n_add;
        } else if (r < 90) {
            std::uniform_int_distribution<size_t> pick(0, live.size() - 1);
            const size_t k = pick(rng);
            const uint64_t id = live[k];
            live[k] = live.back();  // swap-remove: O(1), keeps `live` dense
            live.pop_back();
            Op op{};
            op.type = OpType::Cancel;
            op.id = id;
            w.ops.push_back(op);
            ++w.n_cancel;
        } else {
            Op op{};
            op.type = OpType::Bbo;
            w.ops.push_back(op);
            ++w.n_bbo;
        }
        if ((i + 1) % 100 == 0) {
            Op op{};
            op.type = OpType::Depth;
            w.ops.push_back(op);
            ++w.n_depth;
        }
    }
    return w;
}

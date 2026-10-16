// Rohan Ranjan-10428-Capstone project
//
// builds the random workload once, before any timing starts,
// so every implementation gets the exact same list of operations

#pragma once

#include <cstdint>
#include <random>
#include <vector>

#include "orderbook.hpp"

enum class OpType : uint8_t { Add, Cancel, Bbo };

struct Op {
    OpType   type;
    Side     side;
    double   price;
    long     qty;
    uint64_t id;
};

// default = the assignment workload:
// ~70% add, ~20% cancel, ~10% get_bbo
// side 50/50, qty 1..100, price = random valid tick in that side's band
// cancels always pick a random order that is still live
// (if nothing is live yet, the cancel turns into an add)
//
// the correctness test also uses a "thin book" version: fewer adds, more cancels,
// and prices only within `band` ticks of the middle, so the best levels empty all the time
inline std::vector<Op> make_workload(size_t n, uint32_t seed = 42,
                                     int add_pct = 70, int cancel_pct = 20, int band = 0) {
    std::mt19937 rng(seed);
    std::uniform_int_distribution<int>  pick_op(0, 99);
    std::uniform_int_distribution<int>  pick_side(0, 1);
    std::uniform_int_distribution<long> pick_qty(1, 100);
    int bid_lo = band > 0 ? kMaxBidTick - band + 1 : kMinBidTick;
    int ask_hi = band > 0 ? kMinAskTick + band - 1 : kMaxAskTick;
    std::uniform_int_distribution<int>  pick_bid(bid_lo, kMaxBidTick);
    std::uniform_int_distribution<int>  pick_ask(kMinAskTick, ask_hi);
    const int cancel_end = add_pct + cancel_pct;

    std::vector<Op>       ops;
    std::vector<uint64_t> live; // ids that are still in the book
    ops.reserve(n);
    live.reserve(n);
    uint64_t next_id = 1;

    for (size_t i = 0; i < n; ++i) {
        int r = pick_op(rng);
        if (r >= add_pct && r < cancel_end && !live.empty()) {
            std::uniform_int_distribution<size_t> pick_live(0, live.size() - 1);
            size_t k = pick_live(rng);
            ops.push_back({OpType::Cancel, Side::Buy, 0, 0, live[k]});
            live[k] = live.back(); // swap-remove so picking stays O(1)
            live.pop_back();
        } else if (r >= cancel_end) {
            ops.push_back({OpType::Bbo, Side::Buy, 0, 0, 0});
        } else {
            Side side = pick_side(rng) ? Side::Sell : Side::Buy;
            int  tick = side == Side::Buy ? pick_bid(rng) : pick_ask(rng);
            ops.push_back({OpType::Add, side, to_price(tick), pick_qty(rng), next_id});
            live.push_back(next_id++);
        }
    }
    return ops;
}

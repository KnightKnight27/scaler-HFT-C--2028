#pragma once

#include "types.hpp"
#include <vector>
#include <random>
#include <cstdint>

namespace ob {

enum class OpType : uint8_t {
    Add,
    Cancel,
    Bbo,
    Depth,
};

struct Op {
    OpType  type;
    Side    side;
    Tick    price;
    Qty     qty;
    OrderId id;
};

inline std::vector<Op> make_workload(size_t n, uint32_t seed = 42) {
    std::vector<Op> ops;
    ops.reserve(n);

    std::mt19937 rng(seed);
    std::uniform_int_distribution<int>  percent_dist(0, 99);
    std::uniform_int_distribution<int>  side_dist(0, 1);
    std::uniform_int_distribution<int>  qty_dist(1, 100);
    std::uniform_int_distribution<Tick> bid_price_dist(BID_LO_TICK, BID_HI_TICK);
    std::uniform_int_distribution<Tick> ask_price_dist(ASK_LO_TICK, ASK_HI_TICK);

    std::vector<OrderId> live;
    OrderId next_id = 1;

    for (size_t i = 0; i < n; ++i) {
        // Every 100th op is a Depth query, replacing the regular op.
        if (i != 0 && i % 100 == 0) {
            ops.push_back(Op{OpType::Depth, Side::Buy, 0, 0, 0});
            continue;
        }

        int roll = percent_dist(rng);

        if (roll < 70) {
            // Add
            Side s = (side_dist(rng) == 0) ? Side::Buy : Side::Sell;
            Tick p = (s == Side::Buy) ? bid_price_dist(rng) : ask_price_dist(rng);
            Qty  q = qty_dist(rng);

            ops.push_back(Op{OpType::Add, s, p, q, next_id});
            live.push_back(next_id);
            ++next_id;
        } else if (roll < 90 && !live.empty()) {
            // Cancel — pick a live id, swap-and-pop
            std::uniform_int_distribution<size_t> idx_dist(0, live.size() - 1);
            size_t idx = idx_dist(rng);
            OrderId victim = live[idx];
            live[idx] = live.back();
            live.pop_back();

            ops.push_back(Op{OpType::Cancel, Side::Buy, 0, 0, victim});
        } else {
            // BBO — also the fallback when a Cancel was rolled but live is empty
            ops.push_back(Op{OpType::Bbo, Side::Buy, 0, 0, 0});
        }
    }

    return ops;
}

} // namespace ob
#pragma once

#include "Types.hpp"
#include <vector>
#include <random>

enum class OpType : uint8_t {
    Add,
    Cancel,
    GetBBO
};

struct Operation {
    OpType type;
    Side side;
    uint64_t id;
    double price;
    long qty;
};

inline std::vector<Operation> generate_workload(size_t total_ops = 1000000, uint32_t seed = 42) {
    std::vector<Operation> ops;
    ops.reserve(total_ops);

    std::mt19937 rng(seed);
    std::uniform_int_distribution<int> op_dist(0, 99);       // 0..69 Add (70%), 70..89 Cancel (20%), 90..99 BBO (10%)
    std::uniform_int_distribution<int> side_dist(0, 1);     // 50% Buy, 50% Sell
    std::uniform_int_distribution<long> qty_dist(1, 100);   // 1..100 quantity
    std::uniform_int_distribution<int> bid_tick_dist(0, 4999);     // [50.00, 99.99]
    std::uniform_int_distribution<int> ask_tick_dist(5001, 10000); // [100.01, 150.00]

    std::vector<uint64_t> live_orders;
    live_orders.reserve(total_ops);

    uint64_t next_id = 1;

    for (size_t i = 0; i < total_ops; ++i) {
        int r = op_dist(rng);
        if (r < 70 || live_orders.empty()) {
            // Add order
            uint64_t id = next_id++;
            Side side = (side_dist(rng) == 0) ? Side::Buy : Side::Sell;
            double price = (side == Side::Buy) 
                ? tick_to_price(bid_tick_dist(rng))
                : tick_to_price(ask_tick_dist(rng));
            long qty = qty_dist(rng);

            ops.push_back({OpType::Add, side, id, price, qty});
            live_orders.push_back(id);
        } else if (r < 90) {
            // Cancel order: must select an order that is currently live
            std::uniform_int_distribution<size_t> pick_dist(0, live_orders.size() - 1);
            size_t pick_idx = pick_dist(rng);
            uint64_t cancel_id = live_orders[pick_idx];

            live_orders[pick_idx] = live_orders.back();
            live_orders.pop_back();

            ops.push_back({OpType::Cancel, Side::Buy, cancel_id, 0.0, 0});
        } else {
            // BBO query
            ops.push_back({OpType::GetBBO, Side::Buy, 0, 0.0, 0});
        }
    }

    return ops;
}

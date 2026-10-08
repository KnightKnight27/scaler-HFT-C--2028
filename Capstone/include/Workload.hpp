#pragma once

#include "Types.hpp"
#include <vector>
#include <random>
#include <cstdint>

enum class OpType : uint8_t {
    Add,
    Cancel,
    GetBBO,
    GetDepth
};

struct Operation {
    OpType type;
    Side side{Side::Buy};
    double price{0.0};
    long qty{0};
    uint64_t order_id{0};
};

class WorkloadGenerator {
public:
    static std::vector<Operation> generate(size_t total_ops = 1000000, uint32_t seed = 42) {
        std::vector<Operation> ops;
        ops.reserve(total_ops + (total_ops / 100) + 16);

        std::mt19937 rng(seed);
        std::uniform_int_distribution<int> dist_100(0, 99);
        std::uniform_int_distribution<long> dist_qty(1, 100);
        std::uniform_int_distribution<int> dist_bid_tick(0, 4999);      // [50.00, 99.99]
        std::uniform_int_distribution<int> dist_ask_tick(5001, 10000);  // [100.01, 150.00]
        std::uniform_int_distribution<int> dist_side(0, 1);

        std::vector<uint64_t> live_orders;
        live_orders.reserve(total_ops);

        uint64_t next_id = 1;

        for (size_t i = 0; i < total_ops; ++i) {
            // Periodic depth query every 100 operations
            if (i > 0 && (i % 100 == 0)) {
                Operation op_depth;
                op_depth.type = OpType::GetDepth;
                ops.push_back(op_depth);
            }

            int roll = dist_100(rng);
            Operation op;

            if (roll < 70 || live_orders.empty()) {
                // 70% Add
                op.type = OpType::Add;
                op.order_id = next_id++;
                op.side = (dist_side(rng) == 0) ? Side::Buy : Side::Sell;
                op.qty = dist_qty(rng);

                if (op.side == Side::Buy) {
                    int tick = dist_bid_tick(rng);
                    op.price = tick_to_price(tick);
                } else {
                    int tick = dist_ask_tick(rng);
                    op.price = tick_to_price(tick);
                }

                live_orders.push_back(op.order_id);
            } else if (roll < 90) {
                // 20% Cancel live order
                op.type = OpType::Cancel;
                std::uniform_int_distribution<size_t> dist_live(0, live_orders.size() - 1);
                size_t idx = dist_live(rng);
                op.order_id = live_orders[idx];

                // Fast swap-and-pop removal
                live_orders[idx] = live_orders.back();
                live_orders.pop_back();
            } else {
                // 10% BBO query
                op.type = OpType::GetBBO;
            }

            ops.push_back(op);
        }

        return ops;
    }
};

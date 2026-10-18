#pragma once

#include "types.hpp"

#include <random>
#include <vector>

namespace ob {

enum class OpType { Add, Cancel, Bbo };

struct Operation {
    OpType type;
    OrderId id;
    Side side;
    Price price;
    Quantity qty;
};

inline std::vector<Operation> make_workload(std::size_t count, std::uint32_t seed) {
    std::mt19937 rng(seed);
    std::uniform_int_distribution<int> action(0, 99);
    std::uniform_int_distribution<int> side_choice(0, 1);
    std::uniform_int_distribution<int> price_offset(0, levels_per_side - 1);
    std::uniform_int_distribution<int> quantity(1, 100);
    std::vector<Operation> result;
    std::vector<OrderId> live;
    result.reserve(count);
    OrderId next_id = 1;

    for (std::size_t i = 0; i < count; ++i) {
        const int choice = action(rng);
        if (choice < 70 || (choice < 90 && live.empty())) {
            const Side side = side_choice(rng) == 0 ? Side::Buy : Side::Sell;
            const Price price = (side == Side::Buy ? bid_min : ask_min) + price_offset(rng);
            result.push_back({OpType::Add, next_id, side, price, quantity(rng)});
            live.push_back(next_id++);
        } else if (choice < 90) {
            std::uniform_int_distribution<std::size_t> pick(0, live.size() - 1);
            const std::size_t index = pick(rng);
            result.push_back({OpType::Cancel, live[index], Side::Buy, 0, 0});
            live[index] = live.back();
            live.pop_back();
        } else {
            result.push_back({OpType::Bbo, 0, Side::Buy, 0, 0});
        }
    }
    return result;
}

} // namespace ob


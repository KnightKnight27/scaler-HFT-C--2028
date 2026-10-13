#pragma once

#include "order_book.hpp"

#include <algorithm>
#include <cstdint>
#include <random>
#include <vector>

enum class OperationType { Add, Cancel, Bbo };

struct Operation {
    OperationType type;
    OrderId id = 0;
    Side side = Side::Buy;
    Tick price = 0;
    long quantity = 0;
};

inline std::vector<Operation> make_workload(std::size_t count, std::uint32_t seed = 42) {
    std::mt19937 rng(seed);
    std::uniform_int_distribution<int> action(1, 100);
    std::uniform_int_distribution<int> side(0, 1);
    std::uniform_int_distribution<int> quantity(1, 100);
    std::uniform_int_distribution<int> bid_price(5000, 9999);
    std::uniform_int_distribution<int> ask_price(10001, 15000);
    std::vector<OrderId> live;
    std::vector<Operation> result;
    result.reserve(count);
    OrderId next_id = 1;

    for (std::size_t i = 0; i < count; ++i) {
        const int choice = action(rng);
        if (choice <= 70 || live.empty()) {
            const Side order_side = side(rng) == 0 ? Side::Buy : Side::Sell;
            const OrderId id = next_id++;
            result.push_back({OperationType::Add, id, order_side,
                              order_side == Side::Buy ? bid_price(rng) : ask_price(rng), quantity(rng)});
            live.push_back(id);
        } else if (choice <= 90) {
            std::uniform_int_distribution<std::size_t> pick(0, live.size() - 1);
            const auto index = pick(rng);
            result.push_back({OperationType::Cancel, live[index]});
            live[index] = live.back();
            live.pop_back();
        } else {
            result.push_back({OperationType::Bbo});
        }
    }
    return result;
}

inline void apply(const Operation& operation, OrderBook& book) {
    if (operation.type == OperationType::Add)
        book.add_order(operation.id, operation.side, operation.price, operation.quantity);
    else if (operation.type == OperationType::Cancel)
        book.cancel_order(operation.id);
    else
        (void)book.get_bbo();
}

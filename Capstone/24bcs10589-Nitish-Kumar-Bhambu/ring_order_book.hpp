#pragma once

#include "order_book.hpp"
#include <array>
#include <unordered_map>

class RingOrderBook {
public:
    explicit RingOrderBook(std::size_t capacity = 600000);
    void add_order(unsigned long long id, Side side, int price, long quantity);
    bool cancel_order(unsigned long long id);
    BBO get_bbo() const;
    std::vector<Level> get_bids(int depth = 10) const;
    std::vector<Level> get_asks(int depth = 10) const;

private:
    std::vector<Order> slots;
    std::unordered_map<unsigned long long, std::size_t> orders;
    std::vector<std::size_t> freeSlots;
    std::size_t head = 0;
    std::size_t tail = 0;
    std::size_t freeCount;

    std::array<long, 5000> bids{};
    std::array<long, 5000> asks{};
    int bestBid = 5000;
    int bestAsk = 5000;
};

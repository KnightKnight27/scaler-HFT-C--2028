#pragma once

#include "order_book.hpp"
#include <map>

class MapOrderBook {
public:
    void add_order(unsigned long long id, Side side, int price, long quantity);
    bool cancel_order(unsigned long long id);
    BBO get_bbo() const;
    std::vector<Level> get_bids(int depth = 10) const;
    std::vector<Level> get_asks(int depth = 10) const;

private:
    std::map<unsigned long long, Order> orders;
    std::map<int, long> bids;
    std::map<int, long> asks;
};

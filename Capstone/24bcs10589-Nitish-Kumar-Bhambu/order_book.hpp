#pragma once

#include <vector>

enum class Side { Buy, Sell };

struct Order {
    unsigned long long id;
    Side side;
    long quantity;
    int price;
};

struct Level {
    int price;
    long qty;
};

struct BBO {
    bool has_bid = false;
    int bid_price = 0;
    long bid_qty = 0;

    bool has_ask = false;
    int ask_price = 0;
    long ask_qty = 0;
};

class OrderBook {
public:
    void add_order(unsigned long long id, Side side, int price, long quantity);
    bool cancel_order(unsigned long long id);
    BBO get_bbo() const;
    std::vector<Level> get_bids(int depth = 10) const;
    std::vector<Level> get_asks(int depth = 10) const;

private:
    std::vector<Order> orders;
    std::vector<Level> bids;
    std::vector<Level> asks;
};

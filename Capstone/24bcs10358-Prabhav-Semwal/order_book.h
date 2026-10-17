#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <unordered_map>
#include <vector>

enum class Side { Buy, Sell };

struct Order {
    Side side;
    int price;
    long qty;
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

bool operator==(const Level& a, const Level& b);
bool operator==(const BBO& a, const BBO& b);
void validate(Side side, int price, long qty);

class MapBook {
    std::map<std::uint64_t, Order> orders;
    std::map<int, long, std::greater<int>> bids;
    std::map<int, long> asks;

public:
    void add_order(std::uint64_t id, Side side, int price, long qty);
    bool cancel_order(std::uint64_t id);
    BBO get_bbo() const;
    std::vector<Level> get_bids(int depth = 10) const;
    std::vector<Level> get_asks(int depth = 10) const;
};

class VectorBook {
    std::unordered_map<std::uint64_t, Order> orders;
    std::vector<Level> bids, asks;

public:
    void add_order(std::uint64_t id, Side side, int price, long qty);
    bool cancel_order(std::uint64_t id);
    BBO get_bbo() const;
    std::vector<Level> get_bids(int depth = 10) const;
    std::vector<Level> get_asks(int depth = 10) const;
};

class FlatOrders {
    struct Entry {
        std::uint64_t id = 0;
        Order order{Side::Buy, 0, 0};
        unsigned char state = 0;
    };

    std::vector<Entry> table = std::vector<Entry>(1024);
    std::size_t filled = 0;
    static std::uint64_t hash(std::uint64_t id);
    void grow();

public:
    Order* find(std::uint64_t id);
    bool insert(std::uint64_t id, Order order);
    bool erase(std::uint64_t id);
};

class ArrayBook {
    FlatOrders orders;
    std::array<long, 5000> bids{}, asks{};
    int best_bid = -1;
    int best_ask = 5000;
    void change(const Order& order, long delta);

public:
    void add_order(std::uint64_t id, Side side, int price, long qty);
    bool cancel_order(std::uint64_t id);
    BBO get_bbo() const;
    std::vector<Level> get_bids(int depth = 10) const;
    std::vector<Level> get_asks(int depth = 10) const;
};

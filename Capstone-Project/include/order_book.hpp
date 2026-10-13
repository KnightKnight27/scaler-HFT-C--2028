#pragma once

#include <cstdint>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <vector>

enum class Side { Buy, Sell };
using Tick = int;
using OrderId = std::uint64_t;

struct Level {
    Tick price;
    long quantity;
};

struct BBO {
    bool has_bid = false;
    Tick bid_price = 0;
    long bid_quantity = 0;
    bool has_ask = false;
    Tick ask_price = 0;
    long ask_quantity = 0;
};

struct StoredOrder {
    Side side;
    Tick price;
    long quantity;
};

class OrderBook {
public:
    virtual ~OrderBook() = default;
    virtual void add_order(OrderId id, Side side, Tick price, long quantity) = 0;
    virtual bool cancel_order(OrderId id) = 0;
    virtual BBO get_bbo() const = 0;
    virtual std::vector<Level> get_bids(int depth = 10) const = 0;
    virtual std::vector<Level> get_asks(int depth = 10) const = 0;
};

inline void validate_order(Side side, Tick price, long quantity) {
    if (quantity <= 0) throw std::invalid_argument("quantity must be positive");
    const bool valid = side == Side::Buy ? price >= 5000 && price <= 9999
                                         : price >= 10001 && price <= 15000;
    if (!valid) throw std::invalid_argument("price is outside the side range");
}

inline void validate_new_id(const std::unordered_set<OrderId>& used_ids, OrderId id) {
    if (used_ids.find(id) != used_ids.end()) throw std::invalid_argument("order ID was already used");
}

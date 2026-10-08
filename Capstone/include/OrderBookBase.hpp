#pragma once

#include "Types.hpp"
#include <vector>
#include <string>

class OrderBookBase {
public:
    virtual ~OrderBookBase() = default;

    virtual void add_order(uint64_t id, Side side, double price, long qty) = 0;
    virtual bool cancel_order(uint64_t id) = 0;
    virtual BBO get_bbo() const = 0;
    virtual std::vector<Level> get_bids(int depth = 10) const = 0;
    virtual std::vector<Level> get_asks(int depth = 10) const = 0;

    virtual std::string get_name() const = 0;
};

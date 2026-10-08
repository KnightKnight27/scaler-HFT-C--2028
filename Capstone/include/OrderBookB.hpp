#pragma once

#include "OrderBookBase.hpp"
#include <vector>
#include <unordered_map>
#include <algorithm>

/**
 * @brief Implementation B: Sorted Contiguous Vector (std::vector<Level>)
 * 
 * - Price levels are stored contiguously in dense dynamic arrays.
 * - Bids are sorted in strictly descending order (bids_.front() is best bid).
 * - Asks are sorted in strictly ascending order (asks_.front() is best ask).
 * - Fast binary search (std::lower_bound) is used to locate price levels.
 * - Excellent cache locality for linear traversal and depth queries, but
 *   level additions and removals incur contiguous memory shifting (memmove).
 */
class OrderBookB : public OrderBookBase {
public:
    OrderBookB();
    ~OrderBookB() override = default;

    void add_order(uint64_t id, Side side, double price, long qty) override;
    bool cancel_order(uint64_t id) override;
    BBO get_bbo() const override;
    std::vector<Level> get_bids(int depth = 10) const override;
    std::vector<Level> get_asks(int depth = 10) const override;

    std::string get_name() const override { return "Implementation B (Sorted Contiguous Vector)"; }

    size_t bid_level_count() const { return bids_.size(); }
    size_t ask_level_count() const { return asks_.size(); }
    size_t live_order_count() const { return orders_.size(); }

private:
    std::vector<Level> bids_;
    std::vector<Level> asks_;
    std::unordered_map<uint64_t, OrderRecord> orders_;
};

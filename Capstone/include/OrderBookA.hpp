#pragma once

#include "OrderBookBase.hpp"
#include <map>
#include <unordered_map>
#include <algorithm>

/**
 * @brief Implementation A: Standard Red-Black Tree (std::map)
 * 
 * - Price levels are stored dynamically in balanced binary search trees (Red-Black Trees).
 * - Bids are ordered descending using std::greater<int64_t>.
 * - Asks are ordered ascending using std::less<int64_t>.
 * - Active orders are tracked via std::unordered_map for O(1) average ID lookup.
 * - Nodes are dynamically allocated on the heap during level insertion.
 */
class OrderBookA : public OrderBookBase {
public:
    OrderBookA() = default;
    ~OrderBookA() override = default;

    void add_order(uint64_t id, Side side, double price, long qty) override;
    bool cancel_order(uint64_t id) override;
    BBO get_bbo() const override;
    std::vector<Level> get_bids(int depth = 10) const override;
    std::vector<Level> get_asks(int depth = 10) const override;

    std::string get_name() const override { return "Implementation A (std::map Red-Black Tree)"; }

    // Inspection helpers
    size_t bid_level_count() const { return bids_.size(); }
    size_t ask_level_count() const { return asks_.size(); }
    size_t live_order_count() const { return orders_.size(); }

private:
    std::map<int64_t, long, std::greater<int64_t>> bids_;
    std::map<int64_t, long, std::less<int64_t>> asks_;
    std::unordered_map<uint64_t, OrderRecord> orders_;
};

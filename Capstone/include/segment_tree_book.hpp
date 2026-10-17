
#pragma once

#include "order_book.hpp"

#include <array>
#include <list>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace orderbook {

class SegmentTreeOrderBook final : public OrderBook {
public:
    bool add_order(OrderId id, Side side, Price price, Quantity qty) override;
    bool cancel_order(OrderId id) override;
    BBO get_bbo() const override;

    std::vector<Level> get_bids(std::size_t depth = 10) const override;
    std::vector<Level> get_asks(std::size_t depth = 10) const override;

private:
    static constexpr std::size_t PRICE_COUNT = 5000;
    static constexpr Price BID_MIN = 5000;
    static constexpr Price BID_MAX = 9999;
    static constexpr Price ASK_MIN = 10001;
    static constexpr Price ASK_MAX = 15000;

    struct Order {
        OrderId id;
        Quantity qty;
    };

    struct PriceLevel {
        Quantity total_qty = 0;
        std::list<Order> orders;
    };

    class SegmentTree {
    public:
        void set_active(std::size_t index, bool active);
        int first() const;
        int last() const;
        std::vector<int> get_indices(std::size_t depth, bool descending) const;

    private:
        std::array<int, 4 * PRICE_COUNT> tree_{};

        void update(std::size_t node, std::size_t left, std::size_t right,
                    std::size_t index, bool active);

        int find_first(std::size_t node, std::size_t left,
                       std::size_t right) const;

        int find_last(std::size_t node, std::size_t left,
                      std::size_t right) const;

        void collect(std::size_t node, std::size_t left, std::size_t right,
                     std::size_t depth, bool descending,
                     std::vector<int>& result) const;
    };

    struct OrderLocation {
        Side side;
        std::size_t index;
        std::list<Order>::iterator iterator;
    };

    std::array<PriceLevel, PRICE_COUNT> bids_{};
    std::array<PriceLevel, PRICE_COUNT> asks_{};

    SegmentTree bid_tree_;
    SegmentTree ask_tree_;

    std::unordered_map<OrderId, OrderLocation> order_index_;
    std::unordered_set<OrderId> used_ids_;

    static bool valid_price(Side side, Price price);
    static std::size_t price_to_index(Side side, Price price);
    static Price index_to_price(Side side, std::size_t index);
};

} // namespace orderbook


#pragma once

#include "order_book.hpp"

#include <cstdint>
#include <list>
#include <queue>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace orderbook {

class HeapOrderBook final : public OrderBook {
public:
    bool add_order(
        OrderId id, Side side, Price price, Quantity qty
    ) override;

    bool cancel_order(OrderId id) override;

    BBO get_bbo() const override;

    std::vector<Level> get_bids(
        std::size_t depth = 10
    ) const override;

    std::vector<Level> get_asks(
        std::size_t depth = 10
    ) const override;

private:
    static constexpr Price MIN_BID = 5000;
    static constexpr Price MAX_BID = 9999;
    static constexpr Price MIN_ASK = 10001;
    static constexpr Price MAX_ASK = 15000;

    struct HeapEntry {
        Price price;
        std::uint64_t generation;
    };

    struct BidCompare {
        bool operator()(
            const HeapEntry& a, const HeapEntry& b
        ) const {
            return a.price < b.price;
        }
    };

    struct AskCompare {
        bool operator()(
            const HeapEntry& a, const HeapEntry& b
        ) const {
            return a.price > b.price;
        }
    };

    struct PriceLevel {
        Quantity total_qty = 0;
        std::list<OrderId> fifo;
        std::uint64_t generation = 0;
    };

    struct ActiveOrder {
        Side side;
        Price price;
        Quantity qty;
        std::list<OrderId>::iterator fifo_it;
    };

    using BidHeap = std::priority_queue<
        HeapEntry, std::vector<HeapEntry>, BidCompare
    >;

    using AskHeap = std::priority_queue<
        HeapEntry, std::vector<HeapEntry>, AskCompare
    >;

    std::unordered_map<Price, PriceLevel> bid_levels_;
    std::unordered_map<Price, PriceLevel> ask_levels_;

    std::unordered_map<OrderId, ActiveOrder> active_orders_;
    std::unordered_set<OrderId> used_ids_;

    mutable BidHeap bid_heap_;
    mutable AskHeap ask_heap_;

    std::uint64_t next_generation_ = 1;

    void clean_bid_heap() const;
    void clean_ask_heap() const;

    std::vector<Level> make_depth(
        const std::unordered_map<Price, PriceLevel>& levels,
        std::size_t depth,
        bool bids
    ) const;
};

} // namespace orderbook

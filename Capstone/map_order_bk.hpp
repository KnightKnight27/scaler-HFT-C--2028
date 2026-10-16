#pragma once

#include <cstddef>
#include <cstdint>
#include <list>
#include <map>
#include <unordered_map>
#include <functional>
#include <iterator>
#include "common.hpp"

// Implementation 1: std::map price levels + hash map order index
// bids: std::map<int, PriceLevel, std::greater<int>> (best bid = begin())
// asks: std::map<int, PriceLevel> (best ask = begin())
// each level keeps total_qty + std::list of orders (FIFO)
// orders: unordered_map<id, {side, tick, list iterator}> for direct cancel

struct Order
{
    std::uint64_t id;
    long qty;
};

struct PriceLevel
{
    long total_qty = 0;
    std::list<Order> orders;
};

using BidMap = std::map<int, PriceLevel, std::greater<int>>;
using AskMap = std::map<int, PriceLevel>;

struct OrderRef
{
    Side side;
    int tick;
    std::list<Order>::iterator it;
};

class MapOrderBook
{
private:
    BidMap bids_;
    AskMap asks_;
    std::unordered_map<uint64_t, OrderRef> orders_;

private:
    template <typename MapT>
    void remove_from_level(MapT &book, const OrderRef &ref)
    {
        auto level = book.find(ref.tick);
        level->second.total_qty = level->second.total_qty - ref.it->qty;
        level->second.orders.erase(ref.it);
        if (level->second.orders.empty())
        {
            book.erase(level);
        }
    }

    template <typename MapT>
    std::vector<Level> top_levels(const MapT &book, int depth) const
    {
        std::vector<Level> out;
        if (depth <= 0)
            return out;
        out.reserve(depth);

        for (auto it = book.begin(); it != book.end() && (int)out.size() < depth; ++it)
            out.push_back({it->first, it->second.total_qty});

        return out;
    }

public:
    MapOrderBook()
    {
        orders_.reserve(8192);
    }

    void add_order(uint64_t id, Side side, int price, long qty)
    {
        int tick = price;

        if (qty <= 0 || !is_valid_price(side, tick) || orders_.count(id) != 0)
        {
            return;
        }

        auto &level = (side == Side::Buy ? bids_[tick] : asks_[tick]);

        level.orders.push_back({id, qty});
        level.total_qty = level.total_qty + qty;

        auto it = std::prev(level.orders.end());
        orders_[id] = {side, tick, it};
    }

    bool cancel_order(uint64_t id)
    {
        auto order = orders_.find(id);

        if (order == orders_.end())
        {
            return false;
        }

        if (order->second.side == Side::Buy)
            remove_from_level(bids_, order->second);
        else
            remove_from_level(asks_, order->second);

        orders_.erase(order);
        return true;
    }

    BBO get_bbo() const
    {
        BBO b{};

        if (!bids_.empty())
        {
            auto bid = bids_.begin();

            b.has_bid = true;
            b.bid_price = bid->first;
            b.bid_qty = bid->second.total_qty;
        }

        if (!asks_.empty())
        {
            auto ask = asks_.begin();
            b.has_ask = true;
            b.ask_price = ask->first;
            b.ask_qty = ask->second.total_qty;
        }
        return b;
    }

    std::vector<Level> get_bids(int depth = 10) const
    {
        return top_levels(bids_, depth);
    }

    std::vector<Level> get_asks(int depth = 10) const
    {
        return top_levels(asks_, depth);
    }
};
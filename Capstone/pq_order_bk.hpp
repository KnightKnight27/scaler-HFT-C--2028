#pragma once

#include <queue>
#include <map>
#include <unordered_map>
#include <vector>
#include <functional>
#include "common.hpp"

// Implementation 2: priority queue (heap) with lazy deletion
// bids: max-heap of ticks (best bid = top()), asks: min-heap (best ask = top())
// level qty: unordered_map<tick, total_qty> per side (source of truth)
// stale ticks stay in the heap and are popped once they reach the top
// orders: std::map<id, {side, tick, qty}> for cancel lookup

struct HeapOrder
{
    Side side;
    int tick;
    long qty;
};

class HeapOrderBook
{
private:
    std::priority_queue<int> bid_heap_;
    std::priority_queue<int, std::vector<int>, std::greater<int>> ask_heap_;
    std::unordered_map<int, long> bid_levels_, ask_levels_;
    std::map<uint64_t, HeapOrder> orders_;

private:
    template <typename HeapT>
    void remove_qty(std::unordered_map<int, long> &levels, HeapT &heap, int tick, long qty)
    {
        auto level = levels.find(tick);
        level->second -= qty;

        if (level->second == 0)
        {
            levels.erase(level);
            while (!heap.empty() && levels.count(heap.top()) == 0)
                heap.pop();
        }
    }

    template <typename HeapT>
    std::vector<Level> top_levels(const HeapT &src, const std::unordered_map<int, long> &levels, int depth) const
    {
        std::vector<Level> out;
        if (depth <= 0)
            return out;
        out.reserve(depth);

        HeapT heap = src;
        int last = -1;

        while (!heap.empty() && (int)out.size() < depth)
        {
            int tick = heap.top();
            heap.pop();

            if (tick == last)
                continue;
            auto lvl = levels.find(tick);
            if (lvl == levels.end())
                continue;

            out.push_back({tick, lvl->second});
            last = tick;
        }
        return out;
    }

    template <typename HeapT>
    void add_qty(std::unordered_map<int, long> &levels, HeapT &heap, int tick, long qty)
    {
        auto level = levels.find(tick);
        if (level == levels.end())
        {
            levels.emplace(tick, qty);
            heap.push(tick);
        }
        else
        {
            level->second += qty;
        }
    }

public:
    HeapOrderBook()
    {
        bid_levels_.reserve(8192);
        ask_levels_.reserve(8192);
    }

    void add_order(uint64_t id, Side side, int price, long qty)
    {
        int tick = price;

        if (qty <= 0 || !is_valid_price(side, tick) || orders_.count(id) != 0)
        {
            return;
        }

        if (side == Side::Buy)
            add_qty(bid_levels_, bid_heap_, tick, qty);
        else
            add_qty(ask_levels_, ask_heap_, tick, qty);

        orders_.emplace(id, HeapOrder{side, tick, qty});
    }

    bool cancel_order(uint64_t id)
    {
        auto order = orders_.find(id);

        if (order == orders_.end())
        {
            return false;
        }

        const auto &o = order->second;

        if (o.side == Side::Buy)
            remove_qty(bid_levels_, bid_heap_, o.tick, o.qty);
        else
            remove_qty(ask_levels_, ask_heap_, o.tick, o.qty);

        orders_.erase(order);
        return true;
    }

    BBO get_bbo() const
    {
        BBO b{};

        if (!bid_heap_.empty())
        {
            auto tick = bid_heap_.top();
            b.has_bid = true;
            b.bid_price = tick;
            b.bid_qty = bid_levels_.at(tick);
        }

        if (!ask_heap_.empty())
        {
            auto tick = ask_heap_.top();
            b.has_ask = true;
            b.ask_price = tick;
            b.ask_qty = ask_levels_.at(tick);
        }

        return b;
    }

    std::vector<Level> get_bids(int depth = 10) const
    {
        return top_levels(bid_heap_, bid_levels_, depth);
    }

    std::vector<Level> get_asks(int depth = 10) const
    {
        return top_levels(ask_heap_, ask_levels_, depth);
    }
};
#pragma once

#include <vector>
#include <array>
#include <unordered_map>
#include <cstdint>
#include "common.hpp"

// Implementation 3: contiguous fixed array
// bids: 0..4999 (increasing towards center)
// asks: 5000..9999 (increasing away from center)
// top of book sits together in the center (4999 and 5000)

struct BookLevel
{
    int price = 0;
    long qty = 0;
};

struct OrderInfo
{
    Side side;
    int idx;
    long qty;
};

class FixedOrderBook
{
private:
    static constexpr int TOTAL_LEVELS = 10000;
    static constexpr int BIDS_COUNT = 5000;

    std::array<BookLevel, TOTAL_LEVELS> levels_;
    std::unordered_map<uint64_t, OrderInfo> orders_;

    int best_bid_idx = -1;
    int best_ask_idx = -1;

    int get_idx(Side side, int tick) const
    {
        if (side == Side::Buy)
            return tick - MIN_BID_TICK;
        else
            return BIDS_COUNT + (tick - MIN_ASK_TICK);
    }

public:
    FixedOrderBook()
    {
        orders_.reserve(8192);

        for (int i = 0; i < BIDS_COUNT; ++i)
        {
            levels_[i].price = MIN_BID_TICK + i;
            levels_[i].qty = 0;
        }
        for (int i = 0; i < BIDS_COUNT; ++i)
        {
            levels_[BIDS_COUNT + i].price = MIN_ASK_TICK + i;
            levels_[BIDS_COUNT + i].qty = 0;
        }
    }

    void add_order(uint64_t id, Side side, int price, long qty)
    {
        if (qty <= 0 || !is_valid_price(side, price) || orders_.count(id) != 0)
            return;

        int idx = get_idx(side, price);
        levels_[idx].qty += qty;
        orders_[id] = {side, idx, qty};

        if (side == Side::Buy)
        {
            if (best_bid_idx == -1 || idx > best_bid_idx)
                best_bid_idx = idx;
        }
        else
        {
            if (best_ask_idx == -1 || idx < best_ask_idx)
                best_ask_idx = idx;
        }
    }

    bool cancel_order(uint64_t id)
    {
        auto it = orders_.find(id);
        if (it == orders_.end())
            return false;

        const auto &order = it->second;
        int idx = order.idx;
        levels_[idx].qty -= order.qty;

        if (levels_[idx].qty == 0)
        {
            if (order.side == Side::Buy)
            {
                if (idx == best_bid_idx)
                {
                    while (best_bid_idx >= 0 && levels_[best_bid_idx].qty == 0)
                        best_bid_idx--;
                }
            }
            else
            {
                if (idx == best_ask_idx)
                {
                    while (best_ask_idx < TOTAL_LEVELS && levels_[best_ask_idx].qty == 0)
                        best_ask_idx++;
                    if (best_ask_idx >= TOTAL_LEVELS)
                        best_ask_idx = -1;
                }
            }
        }

        orders_.erase(it);
        return true;
    }

    BBO get_bbo() const
    {
        BBO b{};

        if (best_bid_idx != -1 && levels_[best_bid_idx].qty > 0)
        {
            b.has_bid = true;
            b.bid_price = levels_[best_bid_idx].price;
            b.bid_qty = levels_[best_bid_idx].qty;
        }

        if (best_ask_idx != -1 && levels_[best_ask_idx].qty > 0)
        {
            b.has_ask = true;
            b.ask_price = levels_[best_ask_idx].price;
            b.ask_qty = levels_[best_ask_idx].qty;
        }

        return b;
    }

    std::vector<Level> get_bids(int depth = 10) const
    {
        std::vector<Level> out;
        if (depth <= 0 || best_bid_idx == -1)
            return out;
        out.reserve(depth);

        for (int i = best_bid_idx; i >= 0 && (int)out.size() < depth; --i)
        {
            if (levels_[i].qty > 0)
                out.push_back({levels_[i].price, levels_[i].qty});
        }
        return out;
    }

    std::vector<Level> get_asks(int depth = 10) const
    {
        std::vector<Level> out;
        if (depth <= 0 || best_ask_idx == -1)
            return out;
        out.reserve(depth);

        for (int i = best_ask_idx; i < TOTAL_LEVELS && (int)out.size() < depth; ++i)
        {
            if (levels_[i].qty > 0)
                out.push_back({levels_[i].price, levels_[i].qty});
        }
        return out;
    }
};
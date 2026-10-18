// vector_book.h - Implementation B: sorted contiguous arrays of price levels.
//
//   price levels : one std::vector<{price, total qty}> per side, sorted from the worst price
//                  to the best price, so the best level is always at the back
//   orders       : not kept per level at all. With no matching, the only thing a level needs
//                  is its total qty, so each order lives only in the hash map below
//   best price   : back() of the vector
//   cancel       : std::unordered_map<id, {side, price, qty}>, then a binary search
//                  (std::lower_bound) for the price level
//
// Keeping the best price at the back means a new best level is a push_back and removing
// the best level is a pop_back; only levels created/emptied deep in the book shift memory.
#pragma once

#include <algorithm>
#include <unordered_map>
#include <vector>

#include "common.h"

class VectorOrderBook {
public:
    static constexpr const char* kName = "B: sorted std::vector levels (best at back) + std::unordered_map orders";

    explicit VectorOrderBook(size_t expected_orders = 1 << 16) {
        orders_.reserve(expected_orders);
        bids_.reserve(kLevelsPerSide);  // a side can never have more levels than this,
        asks_.reserve(kLevelsPerSide);  // so the vectors never reallocate
    }

    bool add_order(OrderId id, Side side, Price price, Qty qty) {
        if (!valid_price(side, price) || !valid_qty(qty)) return false;
        auto [it, inserted] = orders_.try_emplace(id, OrderRec{side, price, qty});
        if (!inserted) return false;

        if (side == Side::Buy) {
            add_qty(bids_, price, qty, BidWorse{});
        } else {
            add_qty(asks_, price, qty, AskWorse{});
        }
        return true;
    }

    bool cancel_order(OrderId id) {
        auto it = orders_.find(id);
        if (it == orders_.end()) return false;

        const OrderRec o = it->second;
        orders_.erase(it);
        if (o.side == Side::Buy) {
            remove_qty(bids_, o.price, o.qty, BidWorse{});
        } else {
            remove_qty(asks_, o.price, o.qty, AskWorse{});
        }
        return true;
    }

    BBO get_bbo() const {
        BBO b;
        if (!bids_.empty()) {
            b.has_bid = true;
            b.bid_price = bids_.back().price;
            b.bid_qty = bids_.back().qty;
        }
        if (!asks_.empty()) {
            b.has_ask = true;
            b.ask_price = asks_.back().price;
            b.ask_qty = asks_.back().qty;
        }
        return b;
    }

    std::vector<Level> get_bids(int depth = 10) const { return top_levels(bids_, depth); }
    std::vector<Level> get_asks(int depth = 10) const { return top_levels(asks_, depth); }

private:
    struct LevelEntry {
        Price price;
        Qty qty;
    };

    struct OrderRec {
        Side side;
        Price price;
        Qty qty;
    };

    // "entry is a worse price than p". The vectors are sorted worst -> best under this.
    struct BidWorse {
        bool operator()(const LevelEntry& e, Price p) const { return e.price < p; }
    };
    struct AskWorse {
        bool operator()(const LevelEntry& e, Price p) const { return e.price > p; }
    };

    template <class Worse>
    static void add_qty(std::vector<LevelEntry>& levels, Price price, Qty qty, Worse worse) {
        auto pos = std::lower_bound(levels.begin(), levels.end(), price, worse);
        if (pos != levels.end() && pos->price == price) {
            pos->qty += qty;
        } else {
            levels.insert(pos, LevelEntry{price, qty});  // shifts the better levels up by one
        }
    }

    template <class Worse>
    static void remove_qty(std::vector<LevelEntry>& levels, Price price, Qty qty, Worse worse) {
        auto pos = std::lower_bound(levels.begin(), levels.end(), price, worse);
        // the order was live, so its level must exist
        pos->qty -= qty;
        if (pos->qty == 0) levels.erase(pos);
    }

    static std::vector<Level> top_levels(const std::vector<LevelEntry>& levels, int depth) {
        std::vector<Level> out;
        if (depth <= 0) return out;
        int n = std::min<int>(depth, static_cast<int>(levels.size()));
        out.reserve(n);
        for (auto it = levels.rbegin(); n-- > 0; ++it) out.push_back(Level{it->price, it->qty});
        return out;
    }

    std::vector<LevelEntry> bids_;  // ascending price, best bid = back()
    std::vector<LevelEntry> asks_;  // descending price, best ask = back()
    std::unordered_map<OrderId, OrderRec> orders_;
};

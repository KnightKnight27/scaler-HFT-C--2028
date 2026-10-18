// array_book.h - Implementation C (my own design): price-indexed arrays + bitmap + flat hash.
//
//   price levels : a fixed array of 5000 totals per side, indexed directly by price.
//                  No search is ever needed to find a level: index = price - offset
//   orders       : only in the OrderTable (flat Robin Hood hash table, 16 bytes per order)
//   best price   : cached index per side. Adding can only move it up (one compare);
//                  when the best level empties, LevelBitmap::find_prev finds the next one
//   cancel       : OrderTable lookup gives the side, level index and qty in one probe
//
// Both sides are stored so that a higher index is a better price:
//   bids: index = price - 50.00      (99.99 -> 4999)
//   asks: index = 150.00 - price     (100.01 -> 4999)
// so the code for "best" and "next best" is the same for both sides.
#pragma once

#include <algorithm>
#include <array>
#include <vector>

#include "common.h"
#include "level_bitmap.h"
#include "order_table.h"

class ArrayOrderBook {
public:
    static constexpr const char* kName = "C: price-indexed arrays + bitmap + open-addressing order table";

    explicit ArrayOrderBook(size_t expected_orders = 1 << 16) : orders_(expected_orders) {}

    bool add_order(OrderId id, Side side, Price price, Qty qty) {
        if (!valid_price(side, price) || !valid_qty(qty)) return false;
        const int idx = to_index(side, price);
        if (!orders_.insert(id, static_cast<uint32_t>(qty), static_cast<uint16_t>(idx), side)) return false;

        BookSide& s = side_of(side);
        if (s.qty[idx] == 0) {  // level was empty: mark it, maybe it is the new best
            s.used.set(idx);
            if (idx > s.best) s.best = idx;
        }
        s.qty[idx] += qty;
        return true;
    }

    bool cancel_order(OrderId id) {
        OrderTable::Entry e;
        if (!orders_.take(id, e)) return false;

        BookSide& s = side_of(e.side);
        s.qty[e.level] -= e.qty;
        if (s.qty[e.level] == 0) {
            s.used.clear(e.level);
            if (e.level == s.best) s.best = s.used.find_prev(e.level - 1);
        }
        return true;
    }

    BBO get_bbo() const {
        BBO b;
        const BookSide& bid = sides_[0];
        const BookSide& ask = sides_[1];
        if (bid.best >= 0) {
            b.has_bid = true;
            b.bid_price = to_price(Side::Buy, bid.best);
            b.bid_qty = bid.qty[bid.best];
        }
        if (ask.best >= 0) {
            b.has_ask = true;
            b.ask_price = to_price(Side::Sell, ask.best);
            b.ask_qty = ask.qty[ask.best];
        }
        return b;
    }

    std::vector<Level> get_bids(int depth = 10) const { return top_levels(Side::Buy, depth); }
    std::vector<Level> get_asks(int depth = 10) const { return top_levels(Side::Sell, depth); }

private:
    struct BookSide {
        std::array<Qty, kLevelsPerSide> qty{};  // total qty per level, 0 = empty
        LevelBitmap<kLevelsPerSide> used;       // bit set <=> qty > 0
        int best = -1;                          // index of the best level, -1 = side is empty
    };

    static int to_index(Side side, Price p) { return side == Side::Buy ? p - kBidMin : kAskMax - p; }
    static Price to_price(Side side, int idx) { return side == Side::Buy ? kBidMin + idx : kAskMax - idx; }

    BookSide& side_of(Side side) { return sides_[static_cast<int>(side)]; }

    std::vector<Level> top_levels(Side side, int depth) const {
        std::vector<Level> out;
        if (depth <= 0) return out;
        out.reserve(std::min(depth, kLevelsPerSide));
        const BookSide& s = sides_[static_cast<int>(side)];
        for (int i = s.best; i >= 0 && static_cast<int>(out.size()) < depth; i = s.used.find_prev(i - 1)) {
            out.push_back(Level{to_price(side, i), s.qty[i]});
        }
        return out;
    }

    BookSide sides_[2];  // [0] = bids, [1] = asks
    OrderTable orders_;
};

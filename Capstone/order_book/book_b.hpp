// book_b.hpp - Implementation B: contiguous sorted vectors + direct-indexed order table.
//
//   orders       : std::vector<Rec> indexed DIRECTLY by order id (no hashing at all)
//   price levels : one sorted std::vector<Level> per side (contiguous memory, no per-level nodes)
//   best price   : v.back()  (vectors are sorted so the best level is the LAST element)
//   cancel lookup: orders_[id] gives the price in O(1), then a binary search finds the level
//
//   add_order    O(log L) to find the slot; O(L) memmove only when a NEW level is created
//   cancel_order O(log L) to find the level; O(L) memmove only when the level becomes empty
//   get_bbo      O(1)
//   get_bids/asks O(depth)
//
// ASSUMPTION / TRADE-OFF: order ids are small, mostly sequential integers (id < kMaxId).
// That is what lets us skip hashing. Ids >= kMaxId are rejected (add_order returns false).
#pragma once
#include <algorithm>
#include <vector>

#include "common.hpp"

class BookB {
public:
    static constexpr OrderId kMaxId = 1u << 24;  // 16.7M ids -> at most ~270 MB table

    BookB() : bids_(+1), asks_(-1) {}

    bool add_order(OrderId id, Side side, Price px, long qty) {
        if (qty <= 0 || !valid_price(side, px) || id >= kMaxId) return false;
        if (id >= orders_.size())
            orders_.resize(std::max<std::size_t>(id + 1, orders_.size() * 2));
        Rec& r = orders_[id];
        if (r.live) return false;  // duplicate id
        r = Rec{qty, px, side, true};
        (side == Side::Buy ? bids_ : asks_).add(px, qty);
        return true;
    }

    bool cancel_order(OrderId id) {
        if (id >= orders_.size()) return false;
        Rec& r = orders_[id];
        if (!r.live) return false;  // unknown or already cancelled
        r.live = false;
        (r.side == Side::Buy ? bids_ : asks_).take(r.price, r.qty);
        return true;
    }

    BBO get_bbo() const {
        BBO b{};
        if (!bids_.v.empty()) {
            b.has_bid   = true;
            b.bid_price = bids_.v.back().price;
            b.bid_qty   = bids_.v.back().qty;
        }
        if (!asks_.v.empty()) {
            b.has_ask   = true;
            b.ask_price = asks_.v.back().price;
            b.ask_qty   = asks_.v.back().qty;
        }
        return b;
    }

    std::vector<Level> get_bids(int depth = 10) const { return bids_.top(depth); }
    std::vector<Level> get_asks(int depth = 10) const { return asks_.top(depth); }

private:
    struct Rec {
        long  qty  = 0;
        Price price = 0;
        Side  side = Side::Buy;
        bool  live = false;
    };

    // Levels of one side, sorted by rank ascending where rank = sign * price.
    // Bids: sign=+1, so ascending price, best (highest) bid last.
    // Asks: sign=-1, so descending price, best (lowest) ask last.
    struct Ladder {
        explicit Ladder(int s) : sign(s) {}

        std::vector<Level>::iterator find_pos(Price px) {
            const int rank = sign * px;
            return std::lower_bound(v.begin(), v.end(), rank,
                                    [this](const Level& l, int r) { return sign * l.price < r; });
        }

        void add(Price px, long qty) {
            auto it = find_pos(px);
            if (it != v.end() && it->price == px) it->qty += qty;
            else                                   v.insert(it, Level{px, qty});
        }

        void take(Price px, long qty) {
            auto it = find_pos(px);
            it->qty -= qty;
            if (it->qty == 0) v.erase(it);
        }

        std::vector<Level> top(int depth) const {
            std::vector<Level> out;
            if (depth <= 0) return out;
            out.reserve(std::min<std::size_t>(v.size(), static_cast<std::size_t>(depth)));
            for (auto it = v.rbegin(); it != v.rend() && static_cast<int>(out.size()) < depth; ++it)
                out.push_back(*it);
            return out;
        }

        std::vector<Level> v;
        int sign;
    };

    std::vector<Rec> orders_;
    Ladder bids_;
    Ladder asks_;
};

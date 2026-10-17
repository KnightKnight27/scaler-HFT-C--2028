// Implementation A: ordered std::map price levels + hashed orders.
//
// Idea: keep the two sides as balanced trees keyed by tick, with the tree
// already sorted so the best price is just begin(). Orders are kept in a hash
// table so cancel can find an order by id in roughly constant time and then do a
// single tree update. This is the "obvious" textbook version and I use it as the
// baseline the other two get compared against.
#ifndef OB_BOOK_MAP_HPP
#define OB_BOOK_MAP_HPP

#include "common.hpp"
#include <map>
#include <unordered_map>
#include <functional>

class BookMap {
public:
    BookMap() {
        orders_.reserve(1 << 16);
    }

    const char* name() const { return "A (std::map + unordered_map)"; }

    void add_order(uint64_t id, Side side, double price, long qty) {
        int tick = price_to_tick(price);
        validate_new_order(side, tick, qty);

        auto res = orders_.emplace(id, OrderRec{side, tick, qty});
        if (!res.second) {
            throw std::invalid_argument("duplicate order id");
        }

        if (side == Side::Buy) {
            bids_[tick] += qty;
        } else {
            asks_[tick] += qty;
        }
    }

    bool cancel_order(uint64_t id) {
        auto it = orders_.find(id);
        if (it == orders_.end()) {
            return false;
        }
        const OrderRec& o = it->second;
        if (o.side == Side::Buy) {
            auto lvl = bids_.find(o.tick);
            lvl->second -= o.qty;
            if (lvl->second == 0) {
                bids_.erase(lvl);
            }
        } else {
            auto lvl = asks_.find(o.tick);
            lvl->second -= o.qty;
            if (lvl->second == 0) {
                asks_.erase(lvl);
            }
        }
        orders_.erase(it);
        return true;
    }

    BBO get_bbo() const {
        BBO bbo;
        if (!bids_.empty()) {
            auto it = bids_.begin();           // greatest tick first
            bbo.has_bid = true;
            bbo.bid_price = tick_to_price(it->first);
            bbo.bid_qty = it->second;
        }
        if (!asks_.empty()) {
            auto it = asks_.begin();           // smallest tick first
            bbo.has_ask = true;
            bbo.ask_price = tick_to_price(it->first);
            bbo.ask_qty = it->second;
        }
        return bbo;
    }

    std::vector<Level> get_bids(int depth = 10) const {
        std::vector<Level> out;
        out.reserve(depth);
        for (auto it = bids_.begin(); it != bids_.end() && (int)out.size() < depth; ++it) {
            out.push_back({tick_to_price(it->first), it->second});
        }
        return out;
    }

    std::vector<Level> get_asks(int depth = 10) const {
        std::vector<Level> out;
        out.reserve(depth);
        for (auto it = asks_.begin(); it != asks_.end() && (int)out.size() < depth; ++it) {
            out.push_back({tick_to_price(it->first), it->second});
        }
        return out;
    }

private:
    // Bids sorted high-to-low so begin() is the best bid; asks low-to-high.
    std::map<int, long, std::greater<int>> bids_;
    std::map<int, long> asks_;
    std::unordered_map<uint64_t, OrderRec> orders_;
};

#endif // OB_BOOK_MAP_HPP

#ifndef ORDERBOOK_BOOK_C_HPP
#define ORDERBOOK_BOOK_C_HPP

#include "common.hpp"

//book c: one array slot per price tick, orders in a vector where index = order id
class ArrayOrderBook {
public:
    ArrayOrderBook() {
        qty_at_.resize(MAX_ASK_TICK + 1, 0);  //slot = tick, slots below 5000 are unused
    }

    void add_order(uint64_t id, Side side, double price, long qty) {
        int tick = to_tick(price);

        //assumes ids are small and come in order (0, 1, 2 ...), true for our benchmark
        if (id >= orders_.size()) {
            orders_.resize(id + 1);
        }
        orders_[id] = Order{side, tick, qty, true};
        qty_at_[tick] += qty;

        if (side == Side::Buy && (best_bid_ == NONE || tick > best_bid_)) {
            best_bid_ = tick;
        }
        if (side == Side::Sell && (best_ask_ == NONE || tick < best_ask_)) {
            best_ask_ = tick;
        }
    }

    bool cancel_order(uint64_t id) {
        if (id >= orders_.size() || !orders_[id].live) return false;

        Order& order = orders_[id];
        order.live = false;
        qty_at_[order.tick] -= order.qty;

        //best level just became empty, walk outwards to the next price that has qty
        if (qty_at_[order.tick] == 0 && order.tick == best_bid_) {
            best_bid_ = NONE;
            for (int t = order.tick - 1; t >= MIN_BID_TICK; t--) {
                if (qty_at_[t] > 0) {
                    best_bid_ = t;
                    break;
                }
            }
        }
        if (qty_at_[order.tick] == 0 && order.tick == best_ask_) {
            best_ask_ = NONE;
            for (int t = order.tick + 1; t <= MAX_ASK_TICK; t++) {
                if (qty_at_[t] > 0) {
                    best_ask_ = t;
                    break;
                }
            }
        }
        return true;
    }

    BBO get_bbo() const {
        BBO bbo;
        if (best_bid_ != NONE) {
            bbo.has_bid = true;
            bbo.bid_price = to_price(best_bid_);
            bbo.bid_qty = qty_at_[best_bid_];
        }
        if (best_ask_ != NONE) {
            bbo.has_ask = true;
            bbo.ask_price = to_price(best_ask_);
            bbo.ask_qty = qty_at_[best_ask_];
        }
        return bbo;
    }

    std::vector<Level> get_bids(int depth = 10) const {
        std::vector<Level> out;
        if (best_bid_ == NONE) return out;

        for (int t = best_bid_; t >= MIN_BID_TICK; t--) {
            if ((int)out.size() >= depth) break;
            if (qty_at_[t] > 0) {
                out.push_back({to_price(t), qty_at_[t]});
            }
        }
        return out;
    }

    std::vector<Level> get_asks(int depth = 10) const {
        std::vector<Level> out;
        if (best_ask_ == NONE) return out;

        for (int t = best_ask_; t <= MAX_ASK_TICK; t++) {
            if ((int)out.size() >= depth) break;
            if (qty_at_[t] > 0) {
                out.push_back({to_price(t), qty_at_[t]});
            }
        }
        return out;
    }

private:
    struct Order {
        Side side;
        int tick;
        long qty;
        bool live;
    };

    static constexpr int NONE = -1;  //that side of the book is empty

    std::vector<long> qty_at_;
    std::vector<Order> orders_;
    int best_bid_ = NONE;
    int best_ask_ = NONE;
};

#endif

#pragma once
#include "common.hpp"
#include <array>
#include <vector>

class OrderBookC {
public:
    OrderBookC() : orders_(MAX_ID + 1) {}

    void add_order(uint64_t id, Side side, int price, long qty) {
        if (id >= orders_.size()) orders_.resize(static_cast<size_t>(id) + 1);
        orders_[id] = Slot{side, price, qty, true};

        if (side == Side::Buy) {
            int idx = price - BID_BASE;
            bid_qty_[static_cast<size_t>(idx)] += qty;
            if (!has_bid_ || price > bid_best_) {
                bid_best_ = price;
                has_bid_ = true;
            }
        } else {
            int idx = price - ASK_BASE;
            ask_qty_[static_cast<size_t>(idx)] += qty;
            if (!has_ask_ || price < ask_best_) {
                ask_best_ = price;
                has_ask_ = true;
            }
        }
    }

    bool cancel_order(uint64_t id) {
        if (id >= orders_.size() || !orders_[id].live) return false;

        Slot& o = orders_[id];
        bool was_buy = o.side == Side::Buy;
        int price = o.price;
        long qty = o.qty;
        o.live = false;
        o.qty = 0;

        if (was_buy) {
            size_t idx = static_cast<size_t>(price - BID_BASE);
            bid_qty_[idx] -= qty;
            if (has_bid_ && bid_qty_[static_cast<size_t>(bid_best_ - BID_BASE)] == 0)
                rescan_bid();
        } else {
            size_t idx = static_cast<size_t>(price - ASK_BASE);
            ask_qty_[idx] -= qty;
            if (has_ask_ && ask_qty_[static_cast<size_t>(ask_best_ - ASK_BASE)] == 0)
                rescan_ask();
        }
        return true;
    }

    BBO get_bbo() const {
        BBO bbo{false, 0, 0, false, 0, 0};
        if (has_bid_) {
            bbo.has_bid = true;
            bbo.bid_price = bid_best_;
            bbo.bid_qty = bid_qty_[static_cast<size_t>(bid_best_ - BID_BASE)];
        }
        if (has_ask_) {
            bbo.has_ask = true;
            bbo.ask_price = ask_best_;
            bbo.ask_qty = ask_qty_[static_cast<size_t>(ask_best_ - ASK_BASE)];
        }
        return bbo;
    }

    std::vector<Level> get_bids(int depth = 10) const {
        std::vector<Level> out;
        if (!has_bid_ || depth <= 0) return out;
        out.reserve(static_cast<size_t>(depth));
        for (int p = bid_best_; p >= BID_BASE && static_cast<int>(out.size()) < depth; --p) {
            long q = bid_qty_[static_cast<size_t>(p - BID_BASE)];
            if (q != 0) out.push_back(Level{p, q});
        }
        return out;
    }

    std::vector<Level> get_asks(int depth = 10) const {
        std::vector<Level> out;
        if (!has_ask_ || depth <= 0) return out;
        out.reserve(static_cast<size_t>(depth));
        for (int p = ask_best_; p <= ASK_MAX && static_cast<int>(out.size()) < depth; ++p) {
            long q = ask_qty_[static_cast<size_t>(p - ASK_BASE)];
            if (q != 0) out.push_back(Level{p, q});
        }
        return out;
    }

private:
    struct Slot {
        Side side;
        int price;
        long qty;
        bool live;
    };

    void rescan_bid() {
        for (int p = bid_best_; p >= BID_BASE; --p) {
            if (bid_qty_[static_cast<size_t>(p - BID_BASE)] != 0) {
                bid_best_ = p;
                return;
            }
        }
        has_bid_ = false;
    }

    void rescan_ask() {
        for (int p = ask_best_; p <= ASK_MAX; ++p) {
            if (ask_qty_[static_cast<size_t>(p - ASK_BASE)] != 0) {
                ask_best_ = p;
                return;
            }
        }
        has_ask_ = false;
    }

    std::array<long, LEVELS_PER_SIDE> bid_qty_{};
    std::array<long, LEVELS_PER_SIDE> ask_qty_{};
    std::vector<Slot> orders_;

    int bid_best_ = 0;
    int ask_best_ = 0;
    bool has_bid_ = false;
    bool has_ask_ = false;
};

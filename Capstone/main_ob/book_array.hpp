#pragma once

#include "types.hpp"
#include <array>
#include <vector>

namespace ob {

class BookArray {
public:
    void add_order(OrderId id, Side s, Tick px, Qty q) {
        if (id >= ord_.size()) {
            ord_.resize(id + 1);
        }

        ord_[id] = Order{px, q, s};

        int i = px - MIN_TICK;
        q_[i] += q;

        if (s == Side::Buy) {
            if (bb_px_ == -1 || px > bb_px_) bb_px_ = px;
        } else {
            if (ba_px_ == -1 || px < ba_px_) ba_px_ = px;
        }
    }

    bool cancel_order(OrderId id) {
        if (id >= ord_.size() || ord_[id].q == 0) {
            return false;
        }

        Order& o = ord_[id];
        Qty cq = o.q;
        Tick px = o.px;
        Side s = o.s;

        o.q = 0;

        int i = px - MIN_TICK;
        q_[i] -= cq;

        if (q_[i] == 0) {
            if (s == Side::Buy && px == bb_px_) {
                Tick t = bb_px_ - 1;
                while (t >= MIN_TICK && q_[t - MIN_TICK] == 0) --t;
                bb_px_ = (t >= MIN_TICK) ? t : -1;
            } else if (s == Side::Sell && px == ba_px_) {
                Tick t = ba_px_ + 1;
                while (t <= MAX_TICK && q_[t - MIN_TICK] == 0) ++t;
                ba_px_ = (t <= MAX_TICK) ? t : -1;
            }
        }

        return true;
    }

    BBO get_bbo() const {
        BBO bbo{};
        if (bb_px_ != -1) {
            bbo.has_bid = true;
            bbo.bid_price = bb_px_;
            bbo.bid_qty = q_[bb_px_ - MIN_TICK];
        }
        if (ba_px_ != -1) {
            bbo.has_ask = true;
            bbo.ask_price = ba_px_;
            bbo.ask_qty = q_[ba_px_ - MIN_TICK];
        }
        return bbo;
    }

    std::vector<Level> get_bids(int d = 10) const {
        std::vector<Level> res;
        if (bb_px_ == -1) return res;

        for (Tick t = bb_px_; t >= MIN_TICK && (int)res.size() < d; --t) {
            Qty q = q_[t - MIN_TICK];
            if (q > 0) res.push_back({t, q});
        }
        return res;
    }

    std::vector<Level> get_asks(int d = 10) const {
        std::vector<Level> res;
        if (ba_px_ == -1) return res;

        for (Tick t = ba_px_; t <= MAX_TICK && (int)res.size() < d; ++t) {
            Qty q = q_[t - MIN_TICK];
            if (q > 0) res.push_back({t, q});
        }
        return res;
    }

private:
    struct Order {
        Tick px;
        Qty  q;
        Side s;
    };

    std::array<Qty, NUM_TICKS> q_{};
    std::vector<Order> ord_;
    Tick bb_px_ = -1;
    Tick ba_px_ = -1;
};

} // namespace ob
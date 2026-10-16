#ifndef ORDER_BOOK_IMPLA_H
#define ORDER_BOOK_IMPLA_H

#include "order_book.h"
#include <map>
#include <unordered_map>
#include <tuple>

class OrderBookA : public OrderBook {
private:
    std::map<double, long, std::greater<double>> bids_;
    std::map<double, long> asks_;
    std::unordered_map<uint64_t, std::tuple<double, Side, long>> orders_;

public:
    void add_order(uint64_t id, Side side, double price, long qty) override {
        if (side == Side::Buy) {
            bids_[price] += qty;
        } else {
            asks_[price] += qty;
        }
        orders_[id] = std::make_tuple(price, side, qty);
    }

    bool cancel_order(uint64_t id) override {
        auto it = orders_.find(id);
        if (it == orders_.end()) {
            return false;
        }
        double price = std::get<0>(it->second);
        Side side = std::get<1>(it->second);
        long qty = std::get<2>(it->second);
        bool ok = true;
        if (side == Side::Buy) {
            auto bit = bids_.find(price);
            if (bit == bids_.end()) {
                ok = false;
            } else {
                bit->second -= qty;
                if (bit->second <= 0) {
                    bids_.erase(bit);
                }
            }
        } else {
            auto ait = asks_.find(price);
            if (ait == asks_.end()) {
                ok = false;
            } else {
                ait->second -= qty;
                if (ait->second <= 0) {
                    asks_.erase(ait);
                }
            }
        }
        orders_.erase(it);
        return ok;
    }

    BBO get_bbo() const override {
        BBO bbo{};
        bbo.has_bid = !bids_.empty();
        if (bbo.has_bid) {
            bbo.bid_price = bids_.begin()->first;
            bbo.bid_qty = bids_.begin()->second;
        }
        bbo.has_ask = !asks_.empty();
        if (bbo.has_ask) {
            bbo.ask_price = asks_.begin()->first;
            bbo.ask_qty = asks_.begin()->second;
        }
        return bbo;
    }

    std::vector<Level> get_bids(int depth = 10) const override {
        std::vector<Level> result;
        result.reserve(depth);
        for (auto it = bids_.begin(); it != bids_.end() && static_cast<int>(result.size()) < depth; ++it) {
            result.push_back({it->first, it->second});
        }
        return result;
    }

    std::vector<Level> get_asks(int depth = 10) const override {
        std::vector<Level> result;
        result.reserve(depth);
        for (auto it = asks_.begin(); it != asks_.end() && static_cast<int>(result.size()) < depth; ++it) {
            result.push_back({it->first, it->second});
        }
        return result;
    }
};

#endif // ORDER_BOOK_IMPLA_H
#ifndef ORDER_BOOK_IMPLB_H
#define ORDER_BOOK_IMPLB_H

#include "order_book.h"
#include <vector>
#include <unordered_map>
#include <tuple>
#include <algorithm>

class OrderBookB : public OrderBook {
private:
    std::vector<std::pair<double, long>> bids_;
    std::vector<std::pair<double, long>> asks_;
    std::unordered_map<uint64_t, std::tuple<double, Side, long>> orders_;

    size_t find_bid(double price) const {
        auto lo = std::lower_bound(bids_.begin(), bids_.end(), price,
            [](const std::pair<double, long>& p, double pr) {
                return p.first > pr;
            });
        return static_cast<size_t>(lo - bids_.begin());
    }

    size_t find_ask(double price) const {
        auto lo = std::lower_bound(asks_.begin(), asks_.end(), price,
            [](const std::pair<double, long>& p, double pr) {
                return p.first < pr;
            });
        return static_cast<size_t>(lo - asks_.begin());
    }

public:
    void add_order(uint64_t id, Side side, double price, long qty) override {
        if (side == Side::Buy) {
            size_t idx = find_bid(price);
            if (idx < bids_.size() && bids_[idx].first == price) {
                bids_[idx].second += qty;
            } else {
                bids_.insert(bids_.begin() + idx, {price, qty});
            }
        } else {
            size_t idx = find_ask(price);
            if (idx < asks_.size() && asks_[idx].first == price) {
                asks_[idx].second += qty;
            } else {
                asks_.insert(asks_.begin() + idx, {price, qty});
            }
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
            size_t idx = find_bid(price);
            if (idx >= bids_.size() || bids_[idx].first != price) {
                ok = false;
            } else {
                bids_[idx].second -= qty;
                if (bids_[idx].second <= 0) {
                    bids_.erase(bids_.begin() + idx);
                }
            }
        } else {
            size_t idx = find_ask(price);
            if (idx >= asks_.size() || asks_[idx].first != price) {
                ok = false;
            } else {
                asks_[idx].second -= qty;
                if (asks_[idx].second <= 0) {
                    asks_.erase(asks_.begin() + idx);
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
            bbo.bid_price = bids_.front().first;
            bbo.bid_qty = bids_.front().second;
        }
        bbo.has_ask = !asks_.empty();
        if (bbo.has_ask) {
            bbo.ask_price = asks_.front().first;
            bbo.ask_qty = asks_.front().second;
        }
        return bbo;
    }

    std::vector<Level> get_bids(int depth = 10) const override {
        std::vector<Level> result;
        result.reserve(depth);
        for (size_t i = 0; i < bids_.size() && static_cast<int>(result.size()) < depth; ++i) {
            result.push_back({bids_[i].first, bids_[i].second});
        }
        return result;
    }

    std::vector<Level> get_asks(int depth = 10) const override {
        std::vector<Level> result;
        result.reserve(depth);
        for (size_t i = 0; i < asks_.size() && static_cast<int>(result.size()) < depth; ++i) {
            result.push_back({asks_[i].first, asks_[i].second});
        }
        return result;
    }
};

#endif // ORDER_BOOK_IMPLB_H
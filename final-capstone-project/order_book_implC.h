#ifndef ORDER_BOOK_IMPLC_H
#define ORDER_BOOK_IMPLC_H

#include "order_book.h"
#include <array>
#include <unordered_map>
#include <tuple>
#include <cmath>

class OrderBookC : public OrderBook {
private:
    static constexpr long MIN_BID_TICK = 5000;
    static constexpr long MAX_BID_TICK = 9999;
    static constexpr long MIN_ASK_TICK = 10001;
    static constexpr long MAX_ASK_TICK = 15000;

    static constexpr size_t BID_SIZE = MAX_BID_TICK - MIN_BID_TICK + 1;
    static constexpr size_t ASK_SIZE = MAX_ASK_TICK - MIN_ASK_TICK + 1;

    std::array<long, BID_SIZE> bid_qty_{};
    std::array<long, ASK_SIZE> ask_qty_{};

    long best_bid_tick_ = -1;
    long best_ask_tick_ = -1;

    std::unordered_map<uint64_t, std::tuple<long, Side, long>> orders_;

    static long price_to_tick(double price) {
        return static_cast<long>(price * 100.0 + 0.5);
    }
    static double tick_to_price(long tick) {
        return static_cast<double>(tick) / 100.0;
    }

    void find_new_best_bid() {
        best_bid_tick_ = -1;
        for (long t = MAX_BID_TICK; t >= MIN_BID_TICK; --t) {
            if (bid_qty_[t - MIN_BID_TICK] > 0) {
                best_bid_tick_ = t;
                break;
            }
        }
    }

    void find_new_best_ask() {
        best_ask_tick_ = -1;
        for (long t = MIN_ASK_TICK; t <= MAX_ASK_TICK; ++t) {
            if (ask_qty_[t - MIN_ASK_TICK] > 0) {
                best_ask_tick_ = t;
                break;
            }
        }
    }

public:
    void add_order(uint64_t id, Side side, double price, long qty) override {
        long tick = price_to_tick(price);
        if (side == Side::Buy) {
            bid_qty_[tick - MIN_BID_TICK] += qty;
            if (best_bid_tick_ == -1 || tick > best_bid_tick_) {
                best_bid_tick_ = tick;
            }
        } else {
            ask_qty_[tick - MIN_ASK_TICK] += qty;
            if (best_ask_tick_ == -1 || tick < best_ask_tick_) {
                best_ask_tick_ = tick;
            }
        }
        orders_[id] = std::make_tuple(tick, side, qty);
    }

    bool cancel_order(uint64_t id) override {
        auto it = orders_.find(id);
        if (it == orders_.end()) {
            return false;
        }
        long tick = std::get<0>(it->second);
        Side side = std::get<1>(it->second);
        long qty = std::get<2>(it->second);

        if (side == Side::Buy) {
            bid_qty_[tick - MIN_BID_TICK] -= qty;
            if (bid_qty_[tick - MIN_BID_TICK] <= 0) {
                bid_qty_[tick - MIN_BID_TICK] = 0;
                if (tick == best_bid_tick_) {
                    find_new_best_bid();
                }
            }
        } else {
            ask_qty_[tick - MIN_ASK_TICK] -= qty;
            if (ask_qty_[tick - MIN_ASK_TICK] <= 0) {
                ask_qty_[tick - MIN_ASK_TICK] = 0;
                if (tick == best_ask_tick_) {
                    find_new_best_ask();
                }
            }
        }
        orders_.erase(it);
        return true;
    }

    BBO get_bbo() const override {
        BBO bbo{};
        bbo.has_bid = (best_bid_tick_ != -1);
        if (bbo.has_bid) {
            bbo.bid_price = tick_to_price(best_bid_tick_);
            bbo.bid_qty = bid_qty_[best_bid_tick_ - MIN_BID_TICK];
        }
        bbo.has_ask = (best_ask_tick_ != -1);
        if (bbo.has_ask) {
            bbo.ask_price = tick_to_price(best_ask_tick_);
            bbo.ask_qty = ask_qty_[best_ask_tick_ - MIN_ASK_TICK];
        }
        return bbo;
    }

    std::vector<Level> get_bids(int depth = 10) const override {
        std::vector<Level> result;
        result.reserve(depth);
        if (best_bid_tick_ == -1) {
            return result;
        }
        long tick = best_bid_tick_;
        while (tick >= MIN_BID_TICK && static_cast<int>(result.size()) < depth) {
            long q = bid_qty_[tick - MIN_BID_TICK];
            if (q > 0) {
                result.push_back({tick_to_price(tick), q});
            }
            --tick;
        }
        return result;
    }

    std::vector<Level> get_asks(int depth = 10) const override {
        std::vector<Level> result;
        result.reserve(depth);
        if (best_ask_tick_ == -1) {
            return result;
        }
        long tick = best_ask_tick_;
        while (tick <= MAX_ASK_TICK && static_cast<int>(result.size()) < depth) {
            long q = ask_qty_[tick - MIN_ASK_TICK];
            if (q > 0) {
                result.push_back({tick_to_price(tick), q});
            }
            ++tick;
        }
        return result;
    }
};

#endif // ORDER_BOOK_IMPLC_H
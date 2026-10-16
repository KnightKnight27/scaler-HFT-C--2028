
#include "order_book.h"

#include <cmath>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class ArrayOrderBook : public OrderBook {
private:
    static constexpr int MIN_BID = 5000;
    static constexpr int MAX_BID = 9999;
    static constexpr int MIN_ASK = 10001;
    static constexpr int MAX_ASK = 15000;

    static constexpr int BID_COUNT = MAX_BID - MIN_BID + 1;
    static constexpr int ASK_COUNT = MAX_ASK - MIN_ASK + 1;

    struct Order {
        Side side;
        int price_ticks;
        long qty;
    };

    
    long bids[BID_COUNT] = {};
    long asks[ASK_COUNT] = {};

    std::unordered_map<uint64_t, Order> orders;
    std::unordered_set<uint64_t> used_ids;

    static int to_ticks(double price) {
        return static_cast<int>(std::llround(price * 100.0));
    }

    static double to_price(int ticks) {
        return ticks / 100.0;
    }

public:
    void add_order(uint64_t id, Side side,
                   double price, long qty) override {
        int ticks = to_ticks(price);

        if (used_ids.count(id) != 0 || qty <= 0) {
            return;
        }

        if (side == Side::Buy) {
            if (ticks < MIN_BID || ticks > MAX_BID) {
                return;
            }

            bids[ticks - MIN_BID] += qty;
        } else {
            if (ticks < MIN_ASK || ticks > MAX_ASK) {
                return;
            }

            asks[ticks - MIN_ASK] += qty;
        }

        used_ids.insert(id);
        orders.emplace(id, Order{side, ticks, qty});
    }

    bool cancel_order(uint64_t id) override {
        auto it = orders.find(id);

        if (it == orders.end()) {
            return false;
        }

        Order order = it->second;

        if (order.side == Side::Buy) {
            bids[order.price_ticks - MIN_BID] -= order.qty;
        } else {
            asks[order.price_ticks - MIN_ASK] -= order.qty;
        }

        orders.erase(it);
        return true;
    }

    BBO get_bbo() const override {
        BBO result;

        
        for (int i = BID_COUNT - 1; i >= 0; --i) {
            if (bids[i] > 0) {
                result.has_bid = true;
                result.bid_price = to_price(MIN_BID + i);
                result.bid_qty = bids[i];
                break;
            }
        }

        
        for (int i = 0; i < ASK_COUNT; ++i) {
            if (asks[i] > 0) {
                result.has_ask = true;
                result.ask_price = to_price(MIN_ASK + i);
                result.ask_qty = asks[i];
                break;
            }
        }

        return result;
    }

    std::vector<Level> get_bids(int depth = 10) const override {
        std::vector<Level> result;

        if (depth <= 0) {
            return result;
        }

        for (int i = BID_COUNT - 1;
             i >= 0 && static_cast<int>(result.size()) < depth;
             --i) {
            if (bids[i] > 0) {
                result.push_back(
                    Level{to_price(MIN_BID + i), bids[i]});
            }
        }

        return result;
    }

    std::vector<Level> get_asks(int depth = 10) const override {
        std::vector<Level> result;

        if (depth <= 0) {
            return result;
        }

        for (int i = 0;
             i < ASK_COUNT && static_cast<int>(result.size()) < depth;
             ++i) {
            if (asks[i] > 0) {
                result.push_back(
                    Level{to_price(MIN_ASK + i), asks[i]});
            }
        }

        return result;
    }
};

std::unique_ptr<OrderBook> make_array_order_book() {
    return std::make_unique<ArrayOrderBook>();
}

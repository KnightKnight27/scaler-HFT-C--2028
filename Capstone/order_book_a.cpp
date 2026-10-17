#include "order_book.hpp"

#include <functional>
#include <list>
#include <memory>
#include <map>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace capstone {

class MapOrderBook final : public OrderBook {
private:
    using OrderList = std::list<Order>;
    using BidMap = std::map<Price, OrderList, std::greater<Price>>;
    using AskMap = std::map<Price, OrderList>;

    BidMap bids_;
    AskMap asks_;

    struct Location {
        Side side;
        Price priceTicks;
        OrderList::iterator orderIt;
    };

    std::unordered_map<std::uint64_t, Location> locations_;
    std::unordered_set<std::uint64_t> usedIds_;

    static double toPrice(Price ticks) {
        return static_cast<double>(ticks) / 100.0;
    }

    static bool validPrice(const Order& order) {
        if (order.side == Side::Buy) {
            return order.priceTicks >= 5000 &&
                   order.priceTicks <= 9999;
        }

        return order.priceTicks >= 10001 &&
               order.priceTicks <= 15000;
    }

    static long totalQuantity(const OrderList& orders) {
        long total = 0;
        for (const auto& order : orders) {
            total += order.qty;
        }
        return total;
    }

public:
    void addOrder(const Order& order) override {
        if (order.qty <= 0) {
            throw std::invalid_argument("Quantity must be positive");
        }

        if (!validPrice(order)) {
            throw std::invalid_argument("Invalid order price");
        }

        if (usedIds_.count(order.id) != 0) {
            throw std::invalid_argument("Order ID has already been used");
        }

        // Add to the appropriate price level.
        if (order.side == Side::Buy) {
            auto& orders = bids_[order.priceTicks];
            orders.push_back(order);
            auto it = std::prev(orders.end());

            try {
                locations_.emplace(
                    order.id,
                    Location{order.side, order.priceTicks, it}
                );
            } catch (...) {
                orders.erase(it);
                if (orders.empty()) {
                    bids_.erase(order.priceTicks);
                }
                throw;
            }
        } else {
            auto& orders = asks_[order.priceTicks];
            orders.push_back(order);
            auto it = std::prev(orders.end());

            try {
                locations_.emplace(
                    order.id,
                    Location{order.side, order.priceTicks, it}
                );
            } catch (...) {
                orders.erase(it);
                if (orders.empty()) {
                    asks_.erase(order.priceTicks);
                }
                throw;
            }
        }

        // Permanently record the ID so it cannot be reused.
        try {
            usedIds_.insert(order.id);
        } catch (...) {
            auto location = locations_.find(order.id);

            if (location != locations_.end()) {
                if (location->second.side == Side::Buy) {
                    auto level = bids_.find(location->second.priceTicks);
                    level->second.erase(location->second.orderIt);
                    if (level->second.empty()) {
                        bids_.erase(level);
                    }
                } else {
                    auto level = asks_.find(location->second.priceTicks);
                    level->second.erase(location->second.orderIt);
                    if (level->second.empty()) {
                        asks_.erase(level);
                    }
                }

                locations_.erase(location);
            }

            throw;
        }
    }

    bool removeOrder(std::uint64_t id) override {
        auto location = locations_.find(id);

        if (location == locations_.end()) {
            return false;
        }

        const Side side = location->second.side;
        const Price price = location->second.priceTicks;
        const auto orderIt = location->second.orderIt;

        if (side == Side::Buy) {
            auto level = bids_.find(price);
            level->second.erase(orderIt);

            if (level->second.empty()) {
                bids_.erase(level);
            }
        } else {
            auto level = asks_.find(price);
            level->second.erase(orderIt);

            if (level->second.empty()) {
                asks_.erase(level);
            }
        }

        locations_.erase(location);
        return true;
    }

    BBO getBBO() const override {
        BBO result{};

        if (!bids_.empty()) {
            const auto& [price, orders] = *bids_.begin();
            result.has_bid = true;
            result.bid_price = toPrice(price);
            result.bid_qty = totalQuantity(orders);
        }

        if (!asks_.empty()) {
            const auto& [price, orders] = *asks_.begin();
            result.has_ask = true;
            result.ask_price = toPrice(price);
            result.ask_qty = totalQuantity(orders);
        }

        return result;
    }

    std::vector<Level> getBids(int depth = 10) const override {
        std::vector<Level> result;

        if (depth <= 0) {
            return result;
        }

        result.reserve(static_cast<std::size_t>(depth));

        for (const auto& [price, orders] : bids_) {
            if (result.size() >= static_cast<std::size_t>(depth)) {
                break;
            }

            result.push_back(Level{toPrice(price), totalQuantity(orders)});
        }

        return result;
    }

    std::vector<Level> getAsks(int depth = 10) const override {
        std::vector<Level> result;

        if (depth <= 0) {
            return result;
        }

        result.reserve(static_cast<std::size_t>(depth));

        for (const auto& [price, orders] : asks_) {
            if (result.size() >= static_cast<std::size_t>(depth)) {
                break;
            }

            result.push_back(Level{toPrice(price), totalQuantity(orders)});
        }

        return result;
    }
};


std::unique_ptr<OrderBook> createMapOrderBook() {
    return std::make_unique<MapOrderBook>();
}

} // namespace capstone

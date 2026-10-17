#include "order_book.hpp"

#include <array>
#include <list>
#include <memory>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace capstone {

class ArrayOrderBook final : public OrderBook {
private:
    static constexpr int MIN_TICK = 5000;
    static constexpr int MAX_TICK = 15000;
    static constexpr int LEVEL_COUNT = MAX_TICK - MIN_TICK + 1;

    struct PriceLevel {
        std::list<Order> orders;
        long totalQty = 0;
    };

    struct Location {
        Side side;
        Price priceTicks;
        std::list<Order>::iterator orderIt;
    };

    std::array<PriceLevel, LEVEL_COUNT> bids_;
    std::array<PriceLevel, LEVEL_COUNT> asks_;

    std::unordered_map<std::uint64_t, Location> locations_;
    std::unordered_set<std::uint64_t> usedIds_;

    static std::size_t indexOf(Price ticks) {
        return static_cast<std::size_t>(ticks - MIN_TICK);
    }

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

    const PriceLevel& level(Side side, Price ticks) const {
        if (side == Side::Buy) {
            return bids_[indexOf(ticks)];
        }
        return asks_[indexOf(ticks)];
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

        PriceLevel& priceLevel =
            order.side == Side::Buy
                ? bids_[indexOf(order.priceTicks)]
                : asks_[indexOf(order.priceTicks)];

        priceLevel.orders.push_back(order);
        auto orderIt = std::prev(priceLevel.orders.end());

        try {
            locations_.emplace(
                order.id,
                Location{order.side, order.priceTicks, orderIt}
            );
        } catch (...) {
            priceLevel.orders.erase(orderIt);
            throw;
        }

        try {
            usedIds_.insert(order.id);
        } catch (...) {
            locations_.erase(order.id);
            priceLevel.orders.erase(orderIt);
            throw;
        }

        priceLevel.totalQty += order.qty;
    }

    bool removeOrder(std::uint64_t id) override {
        auto it = locations_.find(id);
        if (it == locations_.end()) {
            return false;
        }

        const Location location = it->second;
        PriceLevel& priceLevel =
            location.side == Side::Buy
                ? bids_[indexOf(location.priceTicks)]
                : asks_[indexOf(location.priceTicks)];

        priceLevel.totalQty -= location.orderIt->qty;
        priceLevel.orders.erase(location.orderIt);
        locations_.erase(it);
        return true;
    }

    BBO getBBO() const override {
        BBO result{};

        for (int tick = MAX_TICK - MIN_TICK; tick >= 0; --tick) {
            const int price = MIN_TICK + tick;

            if (price <= 9999 &&
                bids_[indexOf(price)].totalQty > 0) {
                result.has_bid = true;
                result.bid_price = toPrice(price);
                result.bid_qty = bids_[indexOf(price)].totalQty;
                break;
            }
        }

        for (int price = MIN_TICK; price <= MAX_TICK; ++price) {
            if (price >= 10001 &&
                asks_[indexOf(price)].totalQty > 0) {
                result.has_ask = true;
                result.ask_price = toPrice(price);
                result.ask_qty = asks_[indexOf(price)].totalQty;
                break;
            }
        }

        return result;
    }

    std::vector<Level> getBids(int depth = 10) const override {
        std::vector<Level> result;
        if (depth <= 0) return result;

        result.reserve(static_cast<std::size_t>(depth));

        for (int price = 9999;
             price >= 5000 && result.size() < static_cast<std::size_t>(depth);
             --price) {
            const auto& priceLevel = bids_[indexOf(price)];
            if (priceLevel.totalQty > 0) {
                result.push_back(
                    Level{toPrice(price), priceLevel.totalQty}
                );
            }
        }

        return result;
    }

    std::vector<Level> getAsks(int depth = 10) const override {
        std::vector<Level> result;
        if (depth <= 0) return result;

        result.reserve(static_cast<std::size_t>(depth));

        for (int price = 10001;
             price <= 15000 && result.size() < static_cast<std::size_t>(depth);
             ++price) {
            const auto& priceLevel = asks_[indexOf(price)];
            if (priceLevel.totalQty > 0) {
                result.push_back(
                    Level{toPrice(price), priceLevel.totalQty}
                );
            }
        }

        return result;
    }
};

std::unique_ptr<OrderBook> createArrayOrderBook() {
    return std::make_unique<ArrayOrderBook>();
}

} // namespace capstone

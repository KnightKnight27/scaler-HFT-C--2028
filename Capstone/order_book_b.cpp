#include "order_book.hpp"

#include <functional>
#include <memory>
#include <queue>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace capstone {

class HeapOrderBook final : public OrderBook {
private:
    struct Entry {
        Price priceTicks;
        std::uint64_t id;
        std::uint64_t timestampNs;
    };

    struct BetterBid {
        bool operator()(const Entry& a, const Entry& b) const {
            if (a.priceTicks != b.priceTicks)
                return a.priceTicks < b.priceTicks;
            if (a.timestampNs != b.timestampNs)
                return a.timestampNs > b.timestampNs;
            return a.id > b.id;
        }
    };

    struct BetterAsk {
        bool operator()(const Entry& a, const Entry& b) const {
            if (a.priceTicks != b.priceTicks)
                return a.priceTicks > b.priceTicks;
            if (a.timestampNs != b.timestampNs)
                return a.timestampNs > b.timestampNs;
            return a.id > b.id;
        }
    };

    using BidHeap =
        std::priority_queue<Entry, std::vector<Entry>, BetterBid>;
    using AskHeap =
        std::priority_queue<Entry, std::vector<Entry>, BetterAsk>;

    BidHeap bids_;
    AskHeap asks_;

    std::unordered_map<std::uint64_t, Order> active_;
    std::unordered_set<std::uint64_t> usedIds_;

    // Aggregate quantities are updated on every add and cancellation.
    std::unordered_map<Price, long> bidQty_;
    std::unordered_map<Price, long> askQty_;

    static double toPrice(Price ticks) {
        return static_cast<double>(ticks) / 100.0;
    }

    static bool validPrice(const Order& order) {
        if (order.side == Side::Buy)
            return order.priceTicks >= 5000 &&
                   order.priceTicks <= 9999;

        return order.priceTicks >= 10001 &&
               order.priceTicks <= 15000;
    }

    void cleanBidTop() {
        while (!bids_.empty() &&
               active_.count(bids_.top().id) == 0) {
            bids_.pop();
        }
    }

    void cleanAskTop() {
        while (!asks_.empty() &&
               active_.count(asks_.top().id) == 0) {
            asks_.pop();
        }
    }

public:
    void addOrder(const Order& order) override {
        if (order.qty <= 0)
            throw std::invalid_argument("Quantity must be positive");

        if (!validPrice(order))
            throw std::invalid_argument("Invalid order price");

        if (usedIds_.count(order.id) != 0)
            throw std::invalid_argument("Order ID has already been used");

        // Insert the order and heap entry before updating aggregate state.
        // IDs are permanently consumed after a successful add.
        Entry entry{order.priceTicks, order.id, order.timestampNs};

        if (order.side == Side::Buy) {
            bids_.push(entry);
        } else {
            asks_.push(entry);
        }

        try {
            active_.emplace(order.id, order);
        } catch (...) {
            // A heap entry without an active order is treated as stale.
            throw;
        }

        try {
            usedIds_.insert(order.id);
        } catch (...) {
            active_.erase(order.id);
            throw;
        }

        auto& quantities =
            order.side == Side::Buy ? bidQty_ : askQty_;
        quantities[order.priceTicks] += order.qty;
    }

    bool removeOrder(std::uint64_t id) override {
        auto it = active_.find(id);
        if (it == active_.end())
            return false;

        const Order order = it->second;
        auto& quantities =
            order.side == Side::Buy ? bidQty_ : askQty_;

        auto qtyIt = quantities.find(order.priceTicks);
        qtyIt->second -= order.qty;

        if (qtyIt->second == 0)
            quantities.erase(qtyIt);

        active_.erase(it);
        return true;
    }

    BBO getBBO() const override {
        auto* self = const_cast<HeapOrderBook*>(this);
        self->cleanBidTop();
        self->cleanAskTop();

        BBO result{};

        if (!self->bids_.empty()) {
            const auto& top = self->bids_.top();
            result.has_bid = true;
            result.bid_price = toPrice(top.priceTicks);
            result.bid_qty = bidQty_.at(top.priceTicks);
        }

        if (!self->asks_.empty()) {
            const auto& top = self->asks_.top();
            result.has_ask = true;
            result.ask_price = toPrice(top.priceTicks);
            result.ask_qty = askQty_.at(top.priceTicks);
        }

        return result;
    }

    std::vector<Level> getBids(int depth = 10) const override {
        std::vector<Level> result;
        if (depth <= 0)
            return result;

        auto copy = bids_;

        while (!copy.empty() &&
               result.size() < static_cast<std::size_t>(depth)) {
            const auto top = copy.top();
            copy.pop();

            if (active_.count(top.id) == 0)
                continue;

            if (!result.empty() &&
                result.back().price == toPrice(top.priceTicks))
                continue;

            result.push_back(Level{
                toPrice(top.priceTicks),
                bidQty_.at(top.priceTicks)
            });
        }

        return result;
    }

    std::vector<Level> getAsks(int depth = 10) const override {
        std::vector<Level> result;
        if (depth <= 0)
            return result;

        auto copy = asks_;

        while (!copy.empty() &&
               result.size() < static_cast<std::size_t>(depth)) {
            const auto top = copy.top();
            copy.pop();

            if (active_.count(top.id) == 0)
                continue;

            if (!result.empty() &&
                result.back().price == toPrice(top.priceTicks))
                continue;

            result.push_back(Level{
                toPrice(top.priceTicks),
                askQty_.at(top.priceTicks)
            });
        }

        return result;
    }
};

std::unique_ptr<OrderBook> createHeapOrderBook() {
    return std::make_unique<HeapOrderBook>();
}

} // namespace capstone

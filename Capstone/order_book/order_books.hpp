#pragma once

#include "common.hpp"

#include <array>
#include <functional>
#include <map>
#include <queue>
#include <unordered_map>

namespace orderbook {

class MapOrderBook final : public OrderBook {
    std::map<Tick, long, std::greater<Tick>> bids_;
    std::map<Tick, long> asks_;
    std::unordered_map<std::uint64_t, Order> orders_;

    template <typename Map>
    static std::vector<Level> levels(const Map& map, int depth) {
        std::vector<Level> result;
        if (depth <= 0) return result;
        for (const auto& [tick, qty] : map) {
            result.push_back({to_price(tick), qty});
            if (static_cast<int>(result.size()) == depth) break;
        }
        return result;
    }

public:
    void add_order(std::uint64_t id, Side side, double price, long qty) override {
        const Tick tick = to_tick(price);
        validate_order(side, tick, qty);
        if (orders_.find(id) != orders_.end()) throw std::invalid_argument("order ID is already live");
        orders_.emplace(id, Order{side, tick, qty});
        if (side == Side::Buy) bids_[tick] += qty;
        else asks_[tick] += qty;
    }

    bool cancel_order(std::uint64_t id) override {
        const auto found = orders_.find(id);
        if (found == orders_.end()) return false;
        if (found->second.side == Side::Buy) {
            const auto level = bids_.find(found->second.tick);
            level->second -= found->second.qty;
            if (level->second == 0) bids_.erase(level);
        } else {
            const auto level = asks_.find(found->second.tick);
            level->second -= found->second.qty;
            if (level->second == 0) asks_.erase(level);
        }
        orders_.erase(found);
        return true;
    }

    BBO get_bbo() const override {
        BBO result;
        if (!bids_.empty()) {
            result.has_bid = true;
            result.bid_price = to_price(bids_.begin()->first);
            result.bid_qty = bids_.begin()->second;
        }
        if (!asks_.empty()) {
            result.has_ask = true;
            result.ask_price = to_price(asks_.begin()->first);
            result.ask_qty = asks_.begin()->second;
        }
        return result;
    }

    std::vector<Level> get_bids(int depth = 10) const override { return levels(bids_, depth); }
    std::vector<Level> get_asks(int depth = 10) const override { return levels(asks_, depth); }
};

class HeapOrderBook final : public OrderBook {
    std::unordered_map<Tick, long> bid_levels_;
    std::unordered_map<Tick, long> ask_levels_;
    mutable std::priority_queue<Tick> bid_heap_;
    mutable std::priority_queue<Tick, std::vector<Tick>, std::greater<Tick>> ask_heap_;
    std::unordered_map<std::uint64_t, Order> orders_;

    void clean_bid() const {
        while (!bid_heap_.empty() && bid_levels_.find(bid_heap_.top()) == bid_levels_.end()) bid_heap_.pop();
    }
    void clean_ask() const {
        while (!ask_heap_.empty() && ask_levels_.find(ask_heap_.top()) == ask_levels_.end()) ask_heap_.pop();
    }

    template <typename Heap, typename Levels>
    static std::vector<Level> take_levels(Heap heap, const Levels& levels, int depth) {
        std::vector<Level> result;
        if (depth <= 0) return result;
        Tick last = -1;
        while (!heap.empty() && static_cast<int>(result.size()) < depth) {
            const Tick tick = heap.top();
            heap.pop();
            if (tick != last && levels.find(tick) != levels.end()) {
                result.push_back({to_price(tick), levels.at(tick)});
                last = tick;
            }
        }
        return result;
    }

public:
    void add_order(std::uint64_t id, Side side, double price, long qty) override {
        const Tick tick = to_tick(price);
        validate_order(side, tick, qty);
        if (orders_.find(id) != orders_.end()) throw std::invalid_argument("order ID is already live");
        orders_.emplace(id, Order{side, tick, qty});
        if (side == Side::Buy) {
            if (bid_levels_.find(tick) == bid_levels_.end()) bid_heap_.push(tick);
            bid_levels_[tick] += qty;
        } else {
            if (ask_levels_.find(tick) == ask_levels_.end()) ask_heap_.push(tick);
            ask_levels_[tick] += qty;
        }
    }

    bool cancel_order(std::uint64_t id) override {
        const auto found = orders_.find(id);
        if (found == orders_.end()) return false;
        auto& levels = found->second.side == Side::Buy ? bid_levels_ : ask_levels_;
        const auto level = levels.find(found->second.tick);
        level->second -= found->second.qty;
        if (level->second == 0) levels.erase(level);
        orders_.erase(found);
        return true;
    }

    BBO get_bbo() const override {
        clean_bid();
        clean_ask();
        BBO result;
        if (!bid_heap_.empty()) {
            result.has_bid = true;
            result.bid_price = to_price(bid_heap_.top());
            result.bid_qty = bid_levels_.at(bid_heap_.top());
        }
        if (!ask_heap_.empty()) {
            result.has_ask = true;
            result.ask_price = to_price(ask_heap_.top());
            result.ask_qty = ask_levels_.at(ask_heap_.top());
        }
        return result;
    }

    std::vector<Level> get_bids(int depth = 10) const override {
        clean_bid();
        return take_levels(bid_heap_, bid_levels_, depth);
    }
    std::vector<Level> get_asks(int depth = 10) const override {
        clean_ask();
        return take_levels(ask_heap_, ask_levels_, depth);
    }
};

class ArrayOrderBook final : public OrderBook {
    std::array<long, kTickCount> quantities_{};
    std::unordered_map<std::uint64_t, Order> orders_;
    Tick best_bid_ = -1;
    Tick best_ask_ = -1;

    void find_next_bid() {
        best_bid_ = -1;
        for (Tick tick = kMaxBidTick; tick >= kMinBidTick; --tick) {
            if (quantities_[tick] > 0) {
                best_bid_ = tick;
                return;
            }
        }
    }
    void find_next_ask() {
        best_ask_ = -1;
        for (Tick tick = kMinAskTick; tick <= kMaxAskTick; ++tick) {
            if (quantities_[tick] > 0) {
                best_ask_ = tick;
                return;
            }
        }
    }

public:
    void add_order(std::uint64_t id, Side side, double price, long qty) override {
        const Tick tick = to_tick(price);
        validate_order(side, tick, qty);
        if (orders_.find(id) != orders_.end()) throw std::invalid_argument("order ID is already live");
        orders_.emplace(id, Order{side, tick, qty});
        quantities_[tick] += qty;
        if (side == Side::Buy) {
            if (best_bid_ == -1 || tick > best_bid_) best_bid_ = tick;
        } else if (best_ask_ == -1 || tick < best_ask_) {
            best_ask_ = tick;
        }
    }

    bool cancel_order(std::uint64_t id) override {
        const auto found = orders_.find(id);
        if (found == orders_.end()) return false;
        const Order order = found->second;
        quantities_[order.tick] -= order.qty;
        orders_.erase(found);
        if (quantities_[order.tick] == 0) {
            if (order.side == Side::Buy && order.tick == best_bid_) find_next_bid();
            if (order.side == Side::Sell && order.tick == best_ask_) find_next_ask();
        }
        return true;
    }

    BBO get_bbo() const override {
        BBO result;
        if (best_bid_ != -1) {
            result.has_bid = true;
            result.bid_price = to_price(best_bid_);
            result.bid_qty = quantities_[best_bid_];
        }
        if (best_ask_ != -1) {
            result.has_ask = true;
            result.ask_price = to_price(best_ask_);
            result.ask_qty = quantities_[best_ask_];
        }
        return result;
    }

    std::vector<Level> get_bids(int depth = 10) const override {
        std::vector<Level> result;
        for (Tick tick = best_bid_; tick >= kMinBidTick && static_cast<int>(result.size()) < depth; --tick) {
            if (quantities_[tick] > 0) result.push_back({to_price(tick), quantities_[tick]});
        }
        return result;
    }
    std::vector<Level> get_asks(int depth = 10) const override {
        std::vector<Level> result;
        for (Tick tick = best_ask_; tick <= kMaxAskTick && tick != -1 &&
                             static_cast<int>(result.size()) < depth; ++tick) {
            if (quantities_[tick] > 0) result.push_back({to_price(tick), quantities_[tick]});
        }
        return result;
    }
};

inline std::unique_ptr<OrderBook> make_order_book(char impl) {
    switch (impl) {
    case 'A': return std::make_unique<MapOrderBook>();
    case 'B': return std::make_unique<HeapOrderBook>();
    case 'C': return std::make_unique<ArrayOrderBook>();
    default: throw std::invalid_argument("implementation must be A, B, or C");
    }
}

} // namespace orderbook

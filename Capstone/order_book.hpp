#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <map>
#include <stdexcept>
#include <unordered_map>
#include <vector>

// Prices are cents: 9999 means 99.99. All quantities use 64 bits.
using Qty = std::int64_t;
enum class Side { Buy, Sell };

struct Level {
    int price;
    Qty qty;
};

inline bool operator==(const Level& a, const Level& b) {
    return a.price == b.price && a.qty == b.qty;
}

struct BBO {
    bool has_bid = false;
    int bid_price = 0;
    Qty bid_qty = 0;
    bool has_ask = false;
    int ask_price = 0;
    Qty ask_qty = 0;
};

inline bool operator==(const BBO& a, const BBO& b) {
    return a.has_bid == b.has_bid && a.bid_price == b.bid_price &&
           a.bid_qty == b.bid_qty && a.has_ask == b.has_ask &&
           a.ask_price == b.ask_price && a.ask_qty == b.ask_qty;
}

struct Order {
    Side side;
    int price;
    Qty qty;
};

inline void validate_order(Side side, int price, Qty qty) {
    const bool valid_price = side == Side::Buy
        ? price >= 5000 && price <= 9999
        : side == Side::Sell && price >= 10001 && price <= 15000;
    if (!valid_price || qty <= 0)
        throw std::invalid_argument("invalid side, price or quantity");
}

// A: tree for price levels, hash table for live orders.
class MapBook {
    std::map<int, Qty> bids_, asks_;
    std::unordered_map<std::uint64_t, Order> orders_;

public:
    void add_order(std::uint64_t id, Side side, int price, Qty qty) {
        validate_order(side, price, qty);
        if (!orders_.emplace(id, Order{side, price, qty}).second)
            throw std::invalid_argument("duplicate live order ID");
        auto& levels = side == Side::Buy ? bids_ : asks_;
        levels[price] += qty;
    }

    bool cancel_order(std::uint64_t id) {
        auto it = orders_.find(id);
        if (it == orders_.end()) return false;
        const Order order = it->second;
        auto& levels = order.side == Side::Buy ? bids_ : asks_;
        auto level = levels.find(order.price);
        level->second -= order.qty;
        if (level->second == 0) levels.erase(level);
        orders_.erase(it);
        return true;
    }

    BBO get_bbo() const {
        BBO bbo;
        if (!bids_.empty()) {
            bbo.has_bid = true;
            bbo.bid_price = bids_.rbegin()->first;
            bbo.bid_qty = bids_.rbegin()->second;
        }
        if (!asks_.empty()) {
            bbo.has_ask = true;
            bbo.ask_price = asks_.begin()->first;
            bbo.ask_qty = asks_.begin()->second;
        }
        return bbo;
    }

    std::vector<Level> get_bids(int depth = 10) const {
        std::vector<Level> result;
        if (depth <= 0) return result;
        result.reserve(std::min(bids_.size(), static_cast<std::size_t>(depth)));
        for (auto it = bids_.rbegin(); it != bids_.rend() && depth > 0; ++it, --depth)
            result.push_back({it->first, it->second});
        return result;
    }

    std::vector<Level> get_asks(int depth = 10) const {
        std::vector<Level> result;
        if (depth <= 0) return result;
        result.reserve(std::min(asks_.size(), static_cast<std::size_t>(depth)));
        for (auto it = asks_.begin(); it != asks_.end() && depth > 0; ++it, --depth)
            result.push_back({it->first, it->second});
        return result;
    }
};

// B: sorted, compact price vectors; an ordered tree indexes order IDs.
class VectorBook {
    std::vector<Level> bids_, asks_;
    std::map<std::uint64_t, Order> orders_;

    static auto find_level(std::vector<Level>& levels, int price) {
        return std::lower_bound(levels.begin(), levels.end(), price,
            [](const Level& level, int p) { return level.price < p; });
    }

public:
    void add_order(std::uint64_t id, Side side, int price, Qty qty) {
        validate_order(side, price, qty);
        if (!orders_.emplace(id, Order{side, price, qty}).second)
            throw std::invalid_argument("duplicate live order ID");
        auto& levels = side == Side::Buy ? bids_ : asks_;
        auto it = find_level(levels, price);
        if (it != levels.end() && it->price == price) it->qty += qty;
        else levels.insert(it, {price, qty});
    }

    bool cancel_order(std::uint64_t id) {
        auto it = orders_.find(id);
        if (it == orders_.end()) return false;
        const Order order = it->second;
        auto& levels = order.side == Side::Buy ? bids_ : asks_;
        auto level = find_level(levels, order.price);
        level->qty -= order.qty;
        if (level->qty == 0) levels.erase(level);
        orders_.erase(it);
        return true;
    }

    BBO get_bbo() const {
        BBO bbo;
        if (!bids_.empty()) {
            bbo.has_bid = true;
            bbo.bid_price = bids_.back().price;
            bbo.bid_qty = bids_.back().qty;
        }
        if (!asks_.empty()) {
            bbo.has_ask = true;
            bbo.ask_price = asks_.front().price;
            bbo.ask_qty = asks_.front().qty;
        }
        return bbo;
    }

    std::vector<Level> get_bids(int depth = 10) const {
        std::vector<Level> result;
        if (depth <= 0) return result;
        result.reserve(std::min(bids_.size(), static_cast<std::size_t>(depth)));
        for (auto it = bids_.rbegin(); it != bids_.rend() && depth > 0; ++it, --depth)
            result.push_back(*it);
        return result;
    }

    std::vector<Level> get_asks(int depth = 10) const {
        if (depth <= 0) return {};
        const auto count = std::min(asks_.size(), static_cast<std::size_t>(depth));
        return {asks_.begin(), asks_.begin() + count};
    }
};

// C: each legal price has an array slot. Cache the best occupied slots.
// The ID table points into a vector of orders; cancelled slots can be reused.
class ArrayBook {
    static constexpr int levels_per_side = 5000;
    std::array<Qty, levels_per_side> bids_{};
    std::array<Qty, levels_per_side> asks_{};
    int best_bid_ = -1;
    int best_ask_ = levels_per_side;
    std::vector<Order> orders_;
    std::vector<std::size_t> free_slots_;
    std::unordered_map<std::uint64_t, std::size_t> index_;

public:
    void add_order(std::uint64_t id, Side side, int price, Qty qty) {
        validate_order(side, price, qty);
        const std::size_t slot = free_slots_.empty() ? orders_.size() : free_slots_.back();
        if (!index_.emplace(id, slot).second)
            throw std::invalid_argument("duplicate live order ID");
        if (free_slots_.empty()) orders_.push_back({side, price, qty});
        else {
            orders_[slot] = {side, price, qty};
            free_slots_.pop_back();
        }
        if (side == Side::Buy) {
            const int i = price - 5000;
            bids_[i] += qty;
            best_bid_ = std::max(best_bid_, i);
        } else {
            const int i = price - 10001;
            asks_[i] += qty;
            best_ask_ = std::min(best_ask_, i);
        }
    }

    bool cancel_order(std::uint64_t id) {
        auto it = index_.find(id);
        if (it == index_.end()) return false;
        const std::size_t slot = it->second;
        const Order order = orders_[slot];
        if (order.side == Side::Buy) {
            bids_[order.price - 5000] -= order.qty;
            while (best_bid_ >= 0 && bids_[best_bid_] == 0) --best_bid_;
        } else {
            asks_[order.price - 10001] -= order.qty;
            while (best_ask_ < levels_per_side && asks_[best_ask_] == 0) ++best_ask_;
        }
        index_.erase(it);
        free_slots_.push_back(slot);
        return true;
    }

    BBO get_bbo() const {
        BBO bbo;
        if (best_bid_ >= 0) {
            bbo.has_bid = true;
            bbo.bid_price = best_bid_ + 5000;
            bbo.bid_qty = bids_[best_bid_];
        }
        if (best_ask_ < levels_per_side) {
            bbo.has_ask = true;
            bbo.ask_price = best_ask_ + 10001;
            bbo.ask_qty = asks_[best_ask_];
        }
        return bbo;
    }

    std::vector<Level> get_bids(int depth = 10) const {
        std::vector<Level> result;
        if (depth <= 0) return result;
        result.reserve(std::min(depth, levels_per_side));
        for (int i = best_bid_; i >= 0 && depth > 0; --i) {
            if (bids_[i] != 0) {
                result.push_back({i + 5000, bids_[i]});
                --depth;
            }
        }
        return result;
    }

    std::vector<Level> get_asks(int depth = 10) const {
        std::vector<Level> result;
        if (depth <= 0) return result;
        result.reserve(std::min(depth, levels_per_side));
        for (int i = best_ask_; i < levels_per_side && depth > 0; ++i) {
            if (asks_[i] != 0) {
                result.push_back({i + 10001, asks_[i]});
                --depth;
            }
        }
        return result;
    }
};

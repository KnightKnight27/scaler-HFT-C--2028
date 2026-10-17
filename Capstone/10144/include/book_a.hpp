#pragma once
#include "types.hpp"
#include <map>
#include <unordered_map>

namespace ob {
// Tree levels, hashed individual orders. No retained historical IDs.
class BookA {
    std::map<int, Qty> bids_, asks_;
    std::unordered_map<std::uint64_t, Order> orders_;
    static std::vector<Level> depth(const std::map<int, Qty>& levels, int n, bool reverse) {
        std::vector<Level> out;
        if (n <= 0) return out;
        if (reverse) {
            for (auto it = levels.rbegin(); it != levels.rend() && n-- > 0; ++it)
                out.push_back({it->first, it->second});
        } else {
            for (auto it = levels.begin(); it != levels.end() && n-- > 0; ++it)
                out.push_back({it->first, it->second});
        }
        return out;
    }
public:
    // Reject accidental floating-price calls rather than implicitly truncating.
    template<class Price, std::enable_if_t<std::is_floating_point<Price>::value, int> = 0>
    void add_order(std::uint64_t, Side, Price, Qty) = delete;
    void add_order(std::uint64_t id, Side side, int price, Qty qty) {
        validate(side, price, qty);
        if (orders_.count(id)) throw std::invalid_argument("duplicate live ID");
        auto& levels = side == Side::Buy ? bids_ : asks_;
        auto level = levels.find(price);
        check_sum(level == levels.end() ? 0 : level->second, qty);
        orders_.emplace(id, Order{id, side, price, qty});
        try {
            if (level == levels.end()) levels.emplace(price, qty);
            else level->second += qty;
        } catch (...) { orders_.erase(id); throw; }
    }
    bool cancel_order(std::uint64_t id) {
        auto it = orders_.find(id);
        if (it == orders_.end()) return false;
        const Order o = it->second;
        auto& levels = o.side == Side::Buy ? bids_ : asks_;
        auto level = levels.find(o.price);
        level->second -= o.qty;
        if (level->second == 0) levels.erase(level);
        orders_.erase(it);
        return true;
    }
    BBO get_bbo() const {
        Level bid{}, ask{};
        if (!bids_.empty()) bid = {bids_.rbegin()->first, bids_.rbegin()->second};
        if (!asks_.empty()) ask = {asks_.begin()->first, asks_.begin()->second};
        return make_bbo(bid, ask);
    }
    std::vector<Level> get_bids(int n = 10) const { return depth(bids_, n, true); }
    std::vector<Level> get_asks(int n = 10) const { return depth(asks_, n, false); }
};
} // namespace ob

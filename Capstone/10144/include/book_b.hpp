#pragma once
#include "types.hpp"
#include <algorithm>
#include <map>

namespace ob {
// Sorted contiguous levels; ordered ID tree. Never retain vector iterators.
class BookB {
    std::vector<Level> bids_, asks_;
    std::map<std::uint64_t, Order> orders_;
    static auto locate(std::vector<Level>& v, int price) {
        return std::lower_bound(v.begin(), v.end(), price,
                               [](const Level& l, int p) { return l.price < p; });
    }
    static std::vector<Level> depth(const std::vector<Level>& v, int n, bool reverse) {
        std::vector<Level> out;
        if (n <= 0) return out;
        const auto count = std::min(v.size(), static_cast<std::size_t>(n));
        for (std::size_t i = 0; i < count; ++i) out.push_back(v[reverse ? v.size()-1-i : i]);
        return out;
    }
public:
    // Reject accidental floating-price calls rather than implicitly truncating.
    template<class Price, std::enable_if_t<std::is_floating_point<Price>::value, int> = 0>
    void add_order(std::uint64_t, Side, Price, Qty) = delete;
    void add_order(std::uint64_t id, Side side, int price, Qty qty) {
        validate(side, price, qty);
        if (orders_.count(id)) throw std::invalid_argument("duplicate live ID");
        auto& v = side == Side::Buy ? bids_ : asks_;
        auto it = locate(v, price);
        const bool found = it != v.end() && it->price == price;
        check_sum(found ? it->qty : 0, qty);
        orders_.emplace(id, Order{id, side, price, qty});
        try {
            if (found) it->qty += qty;
            else v.insert(it, Level{price, qty});
        } catch (...) { orders_.erase(id); throw; }
    }
    bool cancel_order(std::uint64_t id) {
        auto order = orders_.find(id);
        if (order == orders_.end()) return false;
        const Order o = order->second;
        auto& v = o.side == Side::Buy ? bids_ : asks_;
        auto it = locate(v, o.price);
        it->qty -= o.qty;
        if (!it->qty) v.erase(it);
        orders_.erase(order);
        return true;
    }
    BBO get_bbo() const {
        return make_bbo(bids_.empty() ? Level{} : bids_.back(),
                        asks_.empty() ? Level{} : asks_.front());
    }
    std::vector<Level> get_bids(int n = 10) const { return depth(bids_, n, true); }
    std::vector<Level> get_asks(int n = 10) const { return depth(asks_, n, false); }
};
} // namespace ob

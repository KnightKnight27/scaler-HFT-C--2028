#pragma once
#include "types.hpp"
#include <array>
#include <unordered_map>

namespace ob {
class BookC {
    struct Ticks {
        std::array<Qty, 5000> qty{};
        std::array<std::uint64_t, 79> occupied{};
        int best = -1;
        // Portable C++17 bit position; at most 63 shifts, no compiler intrinsics.
        static int bit(std::uint64_t word, bool highest) {
            int n = 0;
            if (highest) { while (word >>= 1) ++n; }
            else { while ((word & 1) == 0) { ++n; word >>= 1; } }
            return n; // caller guarantees nonzero word
        }
        void add(int i, Qty q, bool buy) {
            qty[i] += q;
            occupied[i / 64] |= std::uint64_t{1} << (i % 64);
            if (best < 0 || (buy ? i > best : i < best)) best = i;
        }
        void remove(int i, Qty q, bool buy) {
            qty[i] -= q;
            if (qty[i]) return;
            occupied[i / 64] &= ~(std::uint64_t{1} << (i % 64));
            if (best != i) return;
            best = -1;
            for (int k = 0; k < 79; ++k) {
                const int w = buy ? 78-k : k;
                if (occupied[w]) { best = w*64 + bit(occupied[w], buy); break; }
            }
        }
        std::vector<Level> depth(int n, int base, bool buy) const {
            std::vector<Level> out;
            if (n <= 0) return out;
            for (int k = 0; k < 79 && n > 0; ++k) {
                const int w = buy ? 78-k : k;
                auto word = occupied[w];
                while (word && n > 0) {
                    const int b = bit(word, buy), i = w*64+b;
                    out.push_back({base+i, qty[i]});
                    word &= ~(std::uint64_t{1} << b);
                    --n;
                }
            }
            return out;
        }
        Level top(int base) const { return best < 0 ? Level{} : Level{base+best, qty[best]}; }
    } bids_, asks_;
    std::vector<Order> orders_;
    std::unordered_map<std::uint64_t, std::size_t> slots_;
public:
    // Reject accidental floating-price calls rather than implicitly truncating.
    template<class Price, std::enable_if_t<std::is_floating_point<Price>::value, int> = 0>
    void add_order(std::uint64_t, Side, Price, Qty) = delete;
    void add_order(std::uint64_t id, Side side, int price, Qty qty) {
        validate(side, price, qty);
        if (slots_.count(id)) throw std::invalid_argument("duplicate live ID");
        const bool buy = side == Side::Buy;
        auto& ticks = buy ? bids_ : asks_;
        const int i = price - (buy ? 5000 : 10001);
        check_sum(ticks.qty[i], qty);
        orders_.push_back({id, side, price, qty});
        try { slots_.emplace(id, orders_.size()-1); }
        catch (...) { orders_.pop_back(); throw; }
        ticks.add(i, qty, buy);
    }
    bool cancel_order(std::uint64_t id) {
        auto it = slots_.find(id);
        if (it == slots_.end()) return false;
        const auto slot = it->second;
        const Order o = orders_[slot];
        const bool buy = o.side == Side::Buy;
        (buy ? bids_ : asks_).remove(o.price-(buy ? 5000 : 10001), o.qty, buy);
        if (slot != orders_.size()-1) {
            orders_[slot] = orders_.back();
            slots_.find(orders_[slot].id)->second = slot;
        }
        orders_.pop_back();
        slots_.erase(it);
        return true;
    }
    BBO get_bbo() const { return make_bbo(bids_.top(5000), asks_.top(10001)); }
    std::vector<Level> get_bids(int n = 10) const { return bids_.depth(n, 5000, true); }
    std::vector<Level> get_asks(int n = 10) const { return asks_.depth(n, 10001, false); }
};
} // namespace ob

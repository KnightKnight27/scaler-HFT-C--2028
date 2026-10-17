#pragma once

#if !defined(__GNUC__) && !defined(__clang__)
#define __builtin_expect(expr, val) (expr)
#define __builtin_ctzll(x) 0
#endif

#include "types.hpp"
#include <array>
#include <cstdint>
#include <unordered_map>
#include <vector>

class OrderBookC {
public:
    OrderBookC();
    ~OrderBookC() = default;

    void add_order(uint64_t id, Side side, double price, long qty);
    bool cancel_order(uint64_t id);
    BBO get_bbo() const;
    std::vector<Level> get_bids(int depth = 10) const;
    std::vector<Level> get_asks(int depth = 10) const;

private:
    static constexpr size_t MAX_DIRECT_ID = 1050000;
    static constexpr int32_t INVALID_IDX = 5000;
    static constexpr size_t BITSET_WORDS = 79;

    struct OrderSlot {
        long qty = 0;
        uint16_t idx = 0;
        uint8_t side = 0;
        uint8_t active = 0;
    };

    alignas(64) std::array<long, NUM_LEVELS_PER_SIDE> bids_;
    alignas(64) std::array<long, NUM_LEVELS_PER_SIDE> asks_;

    alignas(64) std::array<uint64_t, BITSET_WORDS> bid_bits_;
    alignas(64) std::array<uint64_t, BITSET_WORDS> ask_bits_;

    int32_t best_bid_idx_;
    int32_t best_ask_idx_;

    std::vector<OrderSlot> order_pool_;
    std::unordered_map<uint64_t, OrderSlot> overflow_orders_;

    static inline void set_bit(std::array<uint64_t, BITSET_WORDS>& bits, int32_t idx) noexcept {
        bits[idx >> 6] |= (1ULL << (idx & 63));
    }

    static inline void clear_bit(std::array<uint64_t, BITSET_WORDS>& bits, int32_t idx) noexcept {
        bits[idx >> 6] &= ~(1ULL << (idx & 63));
    }

    static inline int32_t find_next_set_bit(const std::array<uint64_t, BITSET_WORDS>& bits, int32_t start_idx) noexcept {
        int32_t word_idx = start_idx >> 6;
        int32_t bit_offset = start_idx & 63;

        uint64_t word = bits[word_idx] & (~0ULL << bit_offset);
        if (word != 0) {
            return (word_idx << 6) + __builtin_ctzll(word);
        }

        for (++word_idx; word_idx < static_cast<int32_t>(BITSET_WORDS); ++word_idx) {
            if (bits[word_idx] != 0) {
                return (word_idx << 6) + __builtin_ctzll(bits[word_idx]);
            }
        }
        return INVALID_IDX;
    }
};

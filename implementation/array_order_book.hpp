#ifndef ARRAY_ORDER_BOOK_HPP
#define ARRAY_ORDER_BOOK_HPP

#include "types.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

// C: direct price indexing, occupancy words and cached best indices. Orders
// live in an independently implemented open-addressed table (no node per ID).
class ArrayOrderBook {
  public:
    void add_order(std::uint64_t id, Side side, int price_ticks, long qty);
    bool cancel_order(std::uint64_t id);
    BBO get_bbo() const;
    std::vector<Level> get_bids(int depth = 10) const;
    std::vector<Level> get_asks(int depth = 10) const;

  private:
    static constexpr int prices = 5000;
    static constexpr int words = (prices + 63) / 64;
    struct PriceSide {
        std::array<long, prices> qty{};
        std::array<std::uint64_t, words> occupied{};
        int best = -1;
        std::size_t active = 0;
        void add(int index, long amount, bool buy);
        void subtract(int index, long amount, bool buy);
        int next(int from, bool buy) const;
    };
    struct Slot {
        std::uint64_t id = 0;
        Order order{};
        unsigned char state = 0; // 0: never used, 1: live, 2: tombstone
    };
    std::vector<Slot> slots_ = std::vector<Slot>(1024);
    std::size_t live_ = 0;
    std::size_t used_ = 0; // Includes tombstones until a rebuild.
    PriceSide bids_;
    PriceSide asks_;
    static std::uint64_t hash(std::uint64_t id);
    std::size_t locate(std::uint64_t id) const;
    void insert(std::uint64_t id, const Order &order);
    void rebuild(std::size_t capacity);
    static std::vector<Level> depth(const PriceSide &side, int count, bool buy);
};
#endif

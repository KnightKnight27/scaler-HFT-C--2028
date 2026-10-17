#ifndef VECTOR_ORDER_BOOK_HPP
#define VECTOR_ORDER_BOOK_HPP

#include "types.hpp"
#include <cstdint>
#include <map>
#include <vector>

// B: tree lookup for IDs; contiguous, ascending vectors for active levels.
class VectorOrderBook {
  public:
    void add_order(std::uint64_t id, Side side, int price_ticks, long qty);
    bool cancel_order(std::uint64_t id);
    BBO get_bbo() const;
    std::vector<Level> get_bids(int depth = 10) const;
    std::vector<Level> get_asks(int depth = 10) const;

  private:
    std::map<std::uint64_t, Order> orders_;
    std::vector<Level> bids_;
    std::vector<Level> asks_;
};
#endif

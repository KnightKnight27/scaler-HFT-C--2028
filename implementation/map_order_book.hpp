#ifndef MAP_ORDER_BOOK_HPP
#define MAP_ORDER_BOOK_HPP

#include "types.hpp"
#include <cstdint>
#include <map>
#include <unordered_map>
#include <vector>

class MapOrderBook {
  private:
    std::unordered_map<std::uint64_t, Order> orders_;
    std::map<int, long> bids_;
    std::map<int, long> asks_;

  public:
    void add_order(std::uint64_t id, Side side, int price_ticks, long qty);
    bool cancel_order(std::uint64_t id);
    BBO get_bbo() const;
    std::vector<Level> get_bids(int depth = 10) const;
    std::vector<Level> get_asks(int depth = 10) const;
};

#endif
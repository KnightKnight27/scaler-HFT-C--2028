#pragma once

#include "orderBook.hpp"
#include <functional>
#include <map>

// everything in red-black trees: orders by id, levels by tick
class OrderBookMap : public OrderBook {
private:
  std::map<int, long, std::greater<int>> bids;
  std::map<int, long, std::less<int>> asks;
  std::map<uint64_t, Order> orders;

public:
  OrderBookMap() = default;

  void addOrder(uint64_t id, Side side, double price, long qty) override;
  bool cancelOrder(uint64_t id) override;
  BBO getBBO() const override;
  std::vector<Level> getBids(int depth = 10) const override;
  std::vector<Level> getAsks(int depth = 10) const override;
};

#pragma once

#include "types.hpp"

class OrderBook {
public:
  virtual ~OrderBook() = default;

  virtual void addOrder(uint64_t id, Side side, double price, long qty) = 0;
  virtual bool cancelOrder(uint64_t id) = 0;
  virtual BBO getBBO() const = 0;
  virtual std::vector<Level> getBids(int depth = 10) const = 0;
  virtual std::vector<Level> getAsks(int depth = 10) const = 0;
};

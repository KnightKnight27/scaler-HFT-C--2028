#pragma once

#include "orderBook.hpp"
#include <unordered_map>
#include <vector>

class OrderBookArray : public OrderBook {
private:
  struct Slot {
    Order order;
    bool live = false;
  };

  // ids above this go in bigIdOrders so one huge id cant blow up memory
  static const uint64_t maxDirectId = 1 << 24;

  std::vector<long> levelQty;
  std::vector<Slot> slots; // index = order id
  std::unordered_map<uint64_t, Order> bigIdOrders;
  int bestBidTick;
  int bestAskTick;

public:
  OrderBookArray();

  void addOrder(uint64_t id, Side side, double price, long qty) override;
  bool cancelOrder(uint64_t id) override;
  BBO getBBO() const override;
  std::vector<Level> getBids(int depth = 10) const override;
  std::vector<Level> getAsks(int depth = 10) const override;
};

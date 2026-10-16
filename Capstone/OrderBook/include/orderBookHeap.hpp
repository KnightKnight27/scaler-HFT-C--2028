#pragma once

#include "orderBook.hpp"
#include <functional>
#include <queue>
#include <unordered_map>
#include <vector>

// hash maps for orders and level qty, binary heaps for the best price
class OrderBookHeap : public OrderBook {
private:
  struct LevelState {
    long qty = 0;
    bool inHeap = false; // keeps one heap entry per tick
  };

  // mutable so getBBO can drop empty levels lazily
  mutable std::priority_queue<int, std::vector<int>, std::less<int>> bidHeap;
  mutable std::priority_queue<int, std::vector<int>, std::greater<int>> askHeap;
  mutable std::unordered_map<int, LevelState> bidLvl;
  mutable std::unordered_map<int, LevelState> askLvl;

  std::unordered_map<uint64_t, Order> orders;

  void pruneStaleBids() const;
  void pruneStaleAsks() const;

public:
  OrderBookHeap() = default;

  void addOrder(uint64_t id, Side side, double price, long qty) override;
  bool cancelOrder(uint64_t id) override;
  BBO getBBO() const override;
  std::vector<Level> getBids(int depth = 10) const override;
  std::vector<Level> getAsks(int depth = 10) const override;
};

// A: priority queue + lazy deletion

#pragma once
#include <functional>
#include <queue>
#include <unordered_map>
#include "common.h"

class PQBook {
public:

  void add_order(uint64_t id, Side side, int64_t price, int64_t qty) {
    if (side == Side::Buy) {
      if (mBidQty[price] == 0) mBids.push(price); // new level
      mBidQty[price] += qty;
    } else {
      if (mAskQty[price] == 0) mAsks.push(price);
      mAskQty[price] += qty;
    }
    mOrders[id] = {side, price, qty};
  }

  // we can't delete from the middle of a heap, so if a level becomes empty we just count it in delete map
  bool cancel_order(uint64_t id) {
    if (mOrders.count(id) == 0) return false;

    Order o = mOrders[id];
    mOrders.erase(id);

    if (o.side == Side::Buy) {
      mBidQty[o.price] -= o.qty;
      if (mBidQty[o.price] == 0) mBidDelete[o.price]++;
    } else {
      mAskQty[o.price] -= o.qty;
      if (mAskQty[o.price] == 0) mAskDelete[o.price]++;
    }

    return true;
  }

  // lazy deletion: first pop the deleted prices from the top
  BBO get_bbo() {
    while (!mBids.empty() && mBidDelete[mBids.top()] > 0) {
      mBidDelete[mBids.top()]--;
      mBids.pop();
    }

    while (!mAsks.empty() && mAskDelete[mAsks.top()] > 0) {
      mAskDelete[mAsks.top()]--;
      mAsks.pop();
    }

    BBO b{};

    if (!mBids.empty()) {
      b.has_bid = true;
      b.bid_price = mBids.top();
      b.bid_qty = mBidQty[b.bid_price];
    }

    if (!mAsks.empty()) {
      b.has_ask = true;
      b.ask_price = mAsks.top();
      b.ask_qty = mAskQty[b.ask_price];
    }

    return b;
  }

  // heap is not sorted, so copy it and pop
  // skip dead prices and the same price coming twice
  std::vector<Level> get_bids(int depth = 10) {
    std::vector<Level> out;
    std::priority_queue<int64_t> h = mBids;

    while (!h.empty() && (int)out.size() < depth) {
      int64_t p = h.top();
      h.pop();

      if (mBidQty[p] == 0) continue;
      if (!out.empty() && out.back().price == p) continue;

      out.push_back({p, mBidQty[p]});
    }

    return out;
  }

  std::vector<Level> get_asks(int depth = 10) {
    std::vector<Level> out;
    std::priority_queue<int64_t, std::vector<int64_t>, std::greater<int64_t>> h = mAsks;

    while (!h.empty() && (int)out.size() < depth) {
      int64_t p = h.top();
      h.pop();

      if (mAskQty[p] == 0) continue;
      if (!out.empty() && out.back().price == p) continue;

      out.push_back({p, mAskQty[p]});
    }
    
    return out;
  }

private:

  struct Order {
    Side side;
    int64_t price;
    int64_t qty;
  };

  std::priority_queue<int64_t> mBids; // max heap, top = best bid
  std::priority_queue<int64_t, std::vector<int64_t>, std::greater<int64_t>> mAsks; // min heap

  std::unordered_map<int64_t, int64_t> mBidQty; // total qty at each price
  std::unordered_map<int64_t, int64_t> mAskQty;

  std::unordered_map<int64_t, int> mBidDelete; // how many dead copies of a price are in the heap
  std::unordered_map<int64_t, int> mAskDelete;

  std::unordered_map<uint64_t, Order> mOrders;
};

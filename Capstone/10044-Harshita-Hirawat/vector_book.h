// B: sorted arrays of price levels + branchless binary search
// bids sorted high to low, asks sorted low to high, best is at index 0

#pragma once
#include <unordered_map>
#include "common.h"

class VectorBook {
public:

  void add_order(uint64_t id, Side side, int64_t price, int64_t qty) {
    if (side == Side::Buy) {
      size_t i = findBid(price);

      if (i < mBids.size() && mBids[i].price == price)
        mBids[i].qty += qty;
      else
        mBids.insert(mBids.begin() + i, {price, qty}); // everything after i shifts
    } else {
      size_t i = findAsk(price);

      if (i < mAsks.size() && mAsks[i].price == price)
        mAsks[i].qty += qty;
      else
        mAsks.insert(mAsks.begin() + i, {price, qty});
    }

    mOrders[id] = {side, price, qty};
  }

  bool cancel_order(uint64_t id) {
    if (mOrders.count(id) == 0) return false;

    Order o = mOrders[id];
    mOrders.erase(id);

    if (o.side == Side::Buy) {
      size_t i = findBid(o.price);
      mBids[i].qty -= o.qty;

      if (mBids[i].qty == 0) mBids.erase(mBids.begin() + i);
    } else {
      size_t i = findAsk(o.price);
      mAsks[i].qty -= o.qty;

      if (mAsks[i].qty == 0) mAsks.erase(mAsks.begin() + i);
    }
    return true;
  }

  BBO get_bbo() const {
    BBO b{};

    if (!mBids.empty()) {
      b.has_bid = true;
      b.bid_price = mBids[0].price;
      b.bid_qty = mBids[0].qty;
    }

    if (!mAsks.empty()) {
      b.has_ask = true;
      b.ask_price = mAsks[0].price;
      b.ask_qty = mAsks[0].qty;
    }

    return b;
  }

  std::vector<Level> get_bids(int depth = 10) const {
    std::vector<Level> out;
    for (int i = 0; i < (int)mBids.size() && i < depth; i++)
      out.push_back(mBids[i]);
    return out;
  }

  std::vector<Level> get_asks(int depth = 10) const {
    std::vector<Level> out;
    for (int i = 0; i < (int)mAsks.size() && i < depth; i++)
      out.push_back(mAsks[i]);
    return out;
  }

private:

  struct Order {
    Side side;
    int64_t price;
    int64_t qty;
  };

  // branchless binary search
  // no if else, condition is 0 or 1 and we multiply with it
  // bids are high to low, so find first index with price <= p
  size_t findBid(int64_t p) const {
    size_t s = 0;
    size_t length = mBids.size();

    while (length > 0) {
      size_t half = length / 2;

      bool condition = mBids[s + half].price > p; // s + half because s moves
      s += condition * (length - half);

      length = half;
    }

    return s;
  }

  // asks are low to high, so find first index with price >= p
  size_t findAsk(int64_t p) const {
    size_t s = 0;
    size_t length = mAsks.size();

    while (length > 0) {
      size_t half = length / 2;
      bool condition = mAsks[s + half].price < p;
      s += condition * (length - half);
      length = half;
    }

    return s;
  }

  std::vector<Level> mBids;
  std::vector<Level> mAsks;
  std::unordered_map<uint64_t, Order> mOrders;
};

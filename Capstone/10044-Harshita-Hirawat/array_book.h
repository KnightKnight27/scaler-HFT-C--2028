// C: one array for both sides, index = price - 5000
// bids are index 0 to 4999, asks are 5001 to 10000
// so best bid and best ask are next to each other in the middle

#pragma once
#include "common.h"

const int LEVELS = 10001;

class ArrayBook {
public:
  ArrayBook() : mQty(LEVELS, 0), mOrders(1 << 20) {}

  void add_order(uint64_t id, Side side, int64_t price, int64_t qty) {
    if (id >= mOrders.size()) mOrders.resize(id * 2); // only if ids go past 2^20

    int i = price - MIN_BID;
    mQty[i] += qty;
    mOrders[id] = {qty, i};

    if (side == Side::Buy && i > mBestBid) mBestBid = i;
    if (side == Side::Sell && i < mBestAsk) mBestAsk = i;
  }

  bool cancel_order(uint64_t id) {
    if (id >= mOrders.size() || mOrders[id].qty == 0) return false; // qty 0 means not live

    int i = mOrders[id].idx;
    mQty[i] -= mOrders[id].qty;
    mOrders[id].qty = 0;

    // if the best level got empty, go to the next one
    while (mBestBid >= 0 && mQty[mBestBid] == 0) mBestBid--;
    while (mBestAsk < LEVELS && mQty[mBestAsk] == 0) mBestAsk++;

    return true;
  }

  BBO get_bbo() const {
    BBO b{};

    if (mBestBid >= 0) {
      b.has_bid = true;
      b.bid_price = mBestBid + MIN_BID;
      b.bid_qty = mQty[mBestBid];
    }

    if (mBestAsk < LEVELS) {
      b.has_ask = true;
      b.ask_price = mBestAsk + MIN_BID;
      b.ask_qty = mQty[mBestAsk];
    }
    
    return b;
  }

  std::vector<Level> get_bids(int depth = 10) const {
    std::vector<Level> out;
    for (int i = mBestBid; i >= 0 && (int)out.size() < depth; i--)
      if (mQty[i] != 0) out.push_back({i + MIN_BID, mQty[i]});
    return out;
  }

  std::vector<Level> get_asks(int depth = 10) const {
    std::vector<Level> out;
    for (int i = mBestAsk; i < LEVELS && (int)out.size() < depth; i++)
      if (mQty[i] != 0) out.push_back({i + MIN_BID, mQty[i]});
    return out;
  }

private:
  struct Slot {
    int64_t qty;
    int idx;
  };

  std::vector<int64_t> mQty;  // total qty at each price
  std::vector<Slot> mOrders;
  int mBestBid = -1;          // -1 means no bids
  int mBestAsk = LEVELS;      // LEVELS means no asks
};

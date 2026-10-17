// same random ops for bench.cpp and test.cpp
#pragma once
#include <random>
#include "common.h"

const int NUM_OPS = 1000000;
const int ADD = 0;
const int CANCEL = 1;
const int QUERY = 2;

struct Op {
  int type;
  Side side;
  uint64_t id;
  int64_t price;
  int64_t qty;
};

// all ops are made before timing so the rng is not measured
inline std::vector<Op> makeWorkload(int n) {
  std::mt19937 rng(42);
  std::uniform_int_distribution<int> pct(1, 100);
  std::uniform_int_distribution<int> coin(0, 1);
  std::uniform_int_distribution<int64_t> qtyDist(1, 100);
  std::uniform_int_distribution<int64_t> bidDist(MIN_BID, MAX_BID);
  std::uniform_int_distribution<int64_t> askDist(MIN_ASK, MAX_ASK);

  std::vector<Op> ops;
  ops.reserve(n);
  std::vector<uint64_t> live; // ids still in the book
  uint64_t nextId = 1;

  for (int i = 0; i < n; i++) {
    int r = pct(rng);
    Op op{};

    if (r <= 70 || live.empty()) {
      op.type = ADD;
      op.id = nextId++;

      op.side = coin(rng) ? Side::Buy : Side::Sell;
      op.price = (op.side == Side::Buy) ? bidDist(rng) : askDist(rng);
      op.qty = qtyDist(rng);

      live.push_back(op.id);
    } else if (r <= 90) {
      // cancel a random live order, swap with last and pop
      size_t k = std::uniform_int_distribution<size_t>(0, live.size() - 1)(rng);

      op.type = CANCEL;
      op.id = live[k];

      live[k] = live.back();
      live.pop_back();
    } else {
      op.type = QUERY;
    }
    ops.push_back(op);
  }
  
  return ops;
}

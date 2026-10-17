#pragma once
#include <cstdint>
#include <iostream>
#include <vector>

// not using doubles. prices are in ticks (1 tick = 0.01), so 99.98 is 9998.
const int64_t MIN_BID = 5000;
const int64_t MAX_BID = 9999;
const int64_t MIN_ASK = 10001;
const int64_t MAX_ASK = 15000;

enum class Side { Buy, Sell };

struct Level {
  int64_t price;
  int64_t qty;
};

struct BBO {
  bool has_bid;
  int64_t bid_price;
  int64_t bid_qty;
  bool has_ask;
  int64_t ask_price;
  int64_t ask_qty;
};

// 9998 prints as 99.98
inline void printPrice(int64_t p) {
  std::cout << p / 100 << ".";
  if (p % 100 < 10) std::cout << "0";
  std::cout << p % 100;
}

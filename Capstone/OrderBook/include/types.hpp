#pragma once

#include <cmath>
#include <cstdint>
#include <vector>

enum class Side { Buy, Sell };

struct Level {
  double price;
  long qty;
};

struct BBO {
  bool hasBid;
  double bidPrice;
  long bidQty;

  bool hasAsk;
  double askPrice;
  long askQty;
};

// prices are kept as integer ticks inside the books, doubles only at the api
struct Order {
  uint64_t id;
  Side side;
  int tick;
  long qty;
};

namespace MarketConfig {
constexpr double tickSize = 0.01;
constexpr double minBid = 50.00;
constexpr double maxBid = 99.99;
constexpr double midPrice = 100.00;
constexpr double minAsk = 100.01;
constexpr double maxAsk = 150.00;
constexpr int totalLevels = 10001; // ticks 0..10000 cover 50.00..150.00
} // namespace MarketConfig

inline int priceToTick(double price) {
  double priceOffset = price - MarketConfig::minBid;
  int tick = std::round(priceOffset * 100.0);
  return tick;
}

inline double tickToPrice(int tick) {
  double price = MarketConfig::minBid + (tick * 0.01);
  return price;
}

inline bool validTick(int tick) {
  return tick >= 0 && tick < MarketConfig::totalLevels;
}

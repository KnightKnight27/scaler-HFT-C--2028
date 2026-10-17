#ifndef TYPES_HPP
#define TYPES_HPP

#include <cstddef>
#include <cstdint>

constexpr std::size_t MAX_ORDERS = 7000000;

enum class Side : std::uint8_t { Buy, Sell };

struct Level {
  double price{0.0};
  long qty{0};
};

struct BBO {
  bool has_bid{false};
  double bid_price{0.0};
  long bid_qty{0};
  bool has_ask{false};
  double ask_price{0.0};
  long ask_qty{0};
};

#endif
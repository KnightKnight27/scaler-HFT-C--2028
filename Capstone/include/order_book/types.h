#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

using OrderId = std::uint64_t;
using Price = std::int64_t;
using Quantity = std::uint64_t;
using TimestampNs = std::uint64_t;

enum class Side : std::uint8_t { Buy, Sell };

struct Order {
  OrderId id;
  Side side;
  Price price;
  Quantity qty;
  TimestampNs timestamp_ns;
};

struct Trade {
  OrderId maker_id;
  OrderId taker_id;
  Price price;
  Quantity qty;
  TimestampNs timestamp_ns;
};

struct LevelSnapshot {
  Price price;
  Quantity total_qty;
  std::uint32_t order_count;
};

struct BBO {
  std::optional<LevelSnapshot> best_bid;
  std::optional<LevelSnapshot> best_ask;
};

enum class BookError : std::uint8_t { DuplicateOrderId, ZeroQuantity, NonPositivePrice };

constexpr std::string_view describe(BookError error) {
  switch (error) {
  case BookError::DuplicateOrderId:
    return "duplicate order id";
  case BookError::ZeroQuantity:
    return "quantity must be positive";
  case BookError::NonPositivePrice:
    return "price must be positive";
  }
  return "unknown error";
}

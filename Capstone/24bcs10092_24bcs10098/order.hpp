#pragma once

#include <cstdint>
#include <optional>
#include <vector>

namespace orderbook {

using OrderId = std::uint64_t;
using PriceTicks = std::int32_t;
using Quantity = std::int64_t;
using Depth = std::int64_t;

inline constexpr PriceTicks ticks_per_unit = 100;
inline constexpr PriceTicks min_bid_ticks = 5000;
inline constexpr PriceTicks max_bid_ticks = 9999;
inline constexpr PriceTicks min_ask_ticks = 10001;
inline constexpr PriceTicks max_ask_ticks = 15000;
inline constexpr std::int32_t levels_per_side = 5000;

enum class Side { bid, ask };

struct Order {
    OrderId id;
    Side side;
    PriceTicks price;
    Quantity quantity;
};

struct PriceLevel {
    PriceTicks price;
    Quantity total_quantity;
};

struct Bbo {
    std::optional<PriceLevel> bid;
    std::optional<PriceLevel> ask;
};

using DepthSnapshot = std::vector<PriceLevel>;

// Results returned by order book operations.
// duplicate_id includes any ID accepted earlier in this book's lifetime,
// even after cancellation. Rejected adds do not consume IDs.
enum class AddResult {
    added,
    invalid_side,
    invalid_price,
    invalid_quantity,
    duplicate_id,
    quantity_overflow
};

enum class CancelResult { cancelled, not_found };

} // namespace orderbook

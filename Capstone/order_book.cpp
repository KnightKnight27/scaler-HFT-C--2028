#include "order_book.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>

namespace orderbook {

void validate_order(Side side, PriceTicks price_ticks, long qty) {
    const bool valid_side = side == Side::Buy || side == Side::Sell;
    const bool valid_price =
        side == Side::Buy ? (price_ticks >= 5000 && price_ticks <= 9999)
                          : (price_ticks >= 10001 && price_ticks <= 15000);
    if (!valid_side || !valid_price || qty <= 0) {
        throw std::invalid_argument("order has an invalid side, price, or quantity");
    }
}

PriceTicks price_to_ticks(double price) {
    if (!std::isfinite(price)) {
        throw std::invalid_argument("price must be finite");
    }
    const double scaled = price * 100.0;
    if (scaled < std::numeric_limits<PriceTicks>::min() ||
        scaled > std::numeric_limits<PriceTicks>::max()) {
        throw std::invalid_argument("price is outside the supported tick range");
    }
    const auto ticks = static_cast<PriceTicks>(std::llround(scaled));
    if (std::abs(scaled - ticks) > 1e-7) {
        throw std::invalid_argument("price must be an exact multiple of 0.01");
    }
    return ticks;
}

double ticks_to_price(PriceTicks ticks) {
    return static_cast<double>(ticks) / 100.0;
}

}  // namespace orderbook

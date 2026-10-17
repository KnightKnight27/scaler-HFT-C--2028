#include "order_book_types.hpp"

#include <cmath>
#include <stdexcept>

namespace {
constexpr int kBidMin = 5000;
constexpr int kBidMax = 9999;
constexpr int kAskMin = 10001;
constexpr int kAskMax = 15000;
} // namespace

int price_to_ticks(double price, Side side) {
    if (!std::isfinite(price)) {
        throw std::invalid_argument("price must be finite");
    }
    const long long ticks = std::llround(price * 100.0);
    if (std::fabs(price * 100.0 - static_cast<double>(ticks)) > 1e-7) {
        throw std::invalid_argument("price must be a multiple of 0.01");
    }
    const bool valid = side == Side::Buy
                           ? ticks >= kBidMin && ticks <= kBidMax
                           : ticks >= kAskMin && ticks <= kAskMax;
    if (!valid) {
        throw std::invalid_argument("price is outside the side's valid band");
    }
    return static_cast<int>(ticks);
}

double ticks_to_price(int ticks) noexcept {
    return static_cast<double>(ticks) / 100.0;
}

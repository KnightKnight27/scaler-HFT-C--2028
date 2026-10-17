#include "order_book.hpp"
#include <cmath>
#include <stdexcept>

namespace orderbook
{
    int price_to_tick(double price, Side side)
    {
        if (!std::isfinite(price))
            throw std::invalid_argument("price must be finite");
        const long tick = std::lround(price * 100.0);
        if (std::fabs(price * 100.0 - tick) > 1e-7)
            throw std::invalid_argument("price must be a multiple of 0.01");
        const bool valid = side == Side::Buy ? (tick >= 5000 && tick <= 9999)
                                             : (tick >= 10001 && tick <= 15000);
        if (!valid)
            throw std::invalid_argument("price is outside the valid side band");
        return static_cast<int>(tick);
    }
    double tick_to_price(int tick) { return static_cast<double>(tick) / 100.0; }
} // namespace orderbook

#include "OrderBook.hpp"

PriceTick price_to_ticks(double price) {
    return (PriceTick)std::lround(price * 100.0);
}

double ticks_to_price(PriceTick ticks) {
    return ticks / 100.0;
}

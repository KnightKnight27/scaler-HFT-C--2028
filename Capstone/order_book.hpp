#pragma once

#include <cstdint>
#include <vector>

namespace orderbook {

using PriceTicks = int;

enum class Side { Buy, Sell };

struct Level {
    PriceTicks price_ticks;
    long qty;
};

struct BBO {
    bool has_bid = false;
    PriceTicks bid_price_ticks = 0;
    long bid_qty = 0;
    bool has_ask = false;
    PriceTicks ask_price_ticks = 0;
    long ask_qty = 0;
};

PriceTicks price_to_ticks(double price);
double ticks_to_price(PriceTicks ticks);
void validate_order(Side side, PriceTicks price_ticks, long qty);

}  // namespace orderbook

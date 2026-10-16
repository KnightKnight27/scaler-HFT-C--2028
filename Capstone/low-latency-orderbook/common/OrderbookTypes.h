#pragma once

#include <cstdint>

enum class Side { Buy, Sell };

struct Level {
    uint16_t price;
    uint32_t qty;
};

struct BBO {
    bool has_bid;
    uint16_t bid_price;
    uint32_t bid_qty;
    bool has_ask;
    uint16_t ask_price;
    uint32_t ask_qty;
};
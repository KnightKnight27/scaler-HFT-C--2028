#pragma once

#include <cstdint>
#include <vector>
#include <cmath>

enum class Side { Buy, Sell };

using PriceTick = int32_t;

PriceTick price_to_ticks(double price);
double ticks_to_price(PriceTick ticks);

struct Level {
    double price;
    long qty;

    bool operator==(const Level& other) const {
        return price == other.price && qty == other.qty;
    }
};

struct BBO {
    bool has_bid;
    double bid_price;
    long bid_qty;

    bool has_ask;
    double ask_price;
    long ask_qty;

    bool operator==(const BBO& other) const {
        if (has_bid != other.has_bid || has_ask != other.has_ask) return false;
        if (has_bid) {
            if (bid_price != other.bid_price || bid_qty != other.bid_qty) return false;
        }
        if (has_ask) {
            if (ask_price != other.ask_price || ask_qty != other.ask_qty) return false;
        }
        return true;
    }
};

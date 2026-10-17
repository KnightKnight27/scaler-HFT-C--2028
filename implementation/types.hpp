#ifndef ORDER_BOOK_TYPES_HPP
#define ORDER_BOOK_TYPES_HPP

enum class Side { Buy, Sell };

struct Order {
    Side side = Side::Buy;
    int price_ticks = 0;
    long qty = 0;
};

struct Level {
    int price_ticks = 0;
    long qty = 0;
};

struct BBO {
    bool has_bid = false;
    int bid_price_ticks = 0;
    long bid_qty = 0;
    bool has_ask = false;
    int ask_price_ticks = 0;
    long ask_qty = 0;
};

#endif
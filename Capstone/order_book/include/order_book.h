
#ifndef ORDER_BOOK_H
#define ORDER_BOOK_H

#include <cstdint>
#include <vector>
#include <memory>


enum class Side {
    Buy,
    Sell
};


struct Level {
    double price;
    long qty;
};


struct BBO {
    bool has_bid = false;
    double bid_price = 0.0;
    long bid_qty = 0;

    bool has_ask = false;
    double ask_price = 0.0;
    long ask_qty = 0;
};


class OrderBook {
public:
    virtual ~OrderBook() = default;

    virtual void add_order(
        uint64_t id,
        Side side,
        double price,
        long qty
    ) = 0;

    virtual bool cancel_order(uint64_t id) = 0;

    virtual BBO get_bbo() const = 0;

    virtual std::vector<Level> get_bids(int depth = 10) const = 0;

    virtual std::vector<Level> get_asks(int depth = 10) const = 0;
};

std::unique_ptr<OrderBook> make_map_order_book();
std::unique_ptr<OrderBook> make_heap_order_book();
std::unique_ptr<OrderBook> make_array_order_book();

#endif

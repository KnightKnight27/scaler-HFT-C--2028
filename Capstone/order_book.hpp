#pragma once

#include <cstdint>
#include <vector>

namespace orderbook
{

    enum class Side
    {
        Buy,
        Sell
    };
    struct Level
    {
        double price{};
        long qty{};
    };
    struct BBO
    {
        bool has_bid{};
        double bid_price{};
        long bid_qty{};
        bool has_ask{};
        double ask_price{};
        long ask_qty{};
    };

    class OrderBook
    {
    public:
        virtual ~OrderBook() = default;
        virtual void add_order(std::uint64_t id, Side side, double price, long qty) = 0;
        virtual bool cancel_order(std::uint64_t id) = 0;
        virtual BBO get_bbo() const = 0;
        virtual std::vector<Level> get_bids(int depth = 10) const = 0;
        virtual std::vector<Level> get_asks(int depth = 10) const = 0;
    };

    // Prices are represented internally as integer cents; public prices remain doubles.
    int price_to_tick(double price, Side side);
    double tick_to_price(int tick);

} // namespace orderbook

#pragma once

#include <cstdint>
#include <vector>

// Order side: Buy (bid) or Sell (ask)
enum class Side {
    Buy,
    Sell
};

// Represents aggregated volume at a single price level
struct Level {
    double price;
    long qty;

    bool operator==(const Level& other) const {
        // Small epsilon comparison or direct comparison since prices are multiples of 0.01
        return price == other.price && qty == other.qty;
    }
};

// Best Bid and Offer summary
struct BBO {
    bool has_bid;
    double bid_price;
    long bid_qty;
    bool has_ask;
    double ask_price;
    long ask_qty;

    bool operator==(const BBO& other) const {
        if (has_bid != other.has_bid || has_ask != other.has_ask) return false;
        if (has_bid && (bid_price != other.bid_price || bid_qty != other.bid_qty)) return false;
        if (has_ask && (ask_price != other.ask_price || ask_qty != other.ask_qty)) return false;
        return true;
    }
};

// Base interface for all 3 implementations
class OrderBook {
public:
    virtual ~OrderBook() = default;

    // Adds a resting order to the book
    virtual void add_order(uint64_t id, Side side, double price, long qty) = 0;

    // Cancels an existing order by ID. Returns false if order doesn't exist or already cancelled
    virtual bool cancel_order(uint64_t id) = 0;

    // Returns current Best Bid and Best Offer
    virtual BBO get_bbo() const = 0;

    // Returns up to 'depth' price levels of bids (sorted descending: highest to lowest)
    virtual std::vector<Level> get_bids(int depth = 10) const = 0;

    // Returns up to 'depth' price levels of asks (sorted ascending: lowest to highest)
    virtual std::vector<Level> get_asks(int depth = 10) const = 0;
};

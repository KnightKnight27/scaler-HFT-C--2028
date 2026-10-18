#include "verify.hpp"

#include "array_book.hpp"
#include "map_book.hpp"
#include "vector_book.hpp"
#include "workload.hpp"

#include <array>
#include <iostream>
#include <limits>
#include <string>

namespace ob {
namespace {

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

struct View {
    BBO bbo;
    std::vector<Level> bids;
    std::vector<Level> asks;
};

// Deliberately slow oracle: retain individual orders and rebuild levels from scratch.
class ReferenceBook {
public:
    void add_order(OrderId id, Side side, Price price, Quantity qty) {
        orders_.push_back({id, {side, price, qty}});
    }

    bool cancel_order(OrderId id) {
        for (auto it = orders_.begin(); it != orders_.end(); ++it) {
            if (it->first == id) {
                orders_.erase(it);
                return true;
            }
        }
        return false;
    }

    View snapshot() const {
        // Absolute tick indices avoid reusing the implementations' side offsets.
        std::array<Quantity, ask_max + 1> totals{};
        for (const auto& entry : orders_) {
            totals[entry.second.price] += entry.second.qty;
        }
        View view;
        for (Price price = bid_max; price >= bid_min; --price) {
            if (totals[price]) view.bids.push_back({price, totals[price]});
        }
        for (Price price = ask_min; price <= ask_max; ++price) {
            if (totals[price]) view.asks.push_back({price, totals[price]});
        }
        if (!view.bids.empty()) {
            view.bbo.has_bid = true;
            view.bbo.bid_price = view.bids.front().price;
            view.bbo.bid_qty = view.bids.front().qty;
        }
        if (!view.asks.empty()) {
            view.bbo.has_ask = true;
            view.bbo.ask_price = view.asks.front().price;
            view.bbo.ask_qty = view.asks.front().qty;
        }
        return view;
    }

private:
    std::vector<std::pair<OrderId, Order>> orders_;
};

template <typename Book>
void check(const Book& book, const View& expected, const char* name, bool check_depth) {
    require(book.get_bbo() == expected.bbo, std::string(name) + ": BBO mismatch");
    require(book.get_bids(levels_per_side) == expected.bids,
            std::string(name) + ": bid levels mismatch");
    require(book.get_asks(levels_per_side) == expected.asks,
            std::string(name) + ": ask levels mismatch");
    if (check_depth) {
        for (int depth : {-1, 0, 1, 10, 6000}) {
            const auto bid_count = depth <= 0 ? 0 : std::min(expected.bids.size(),
                                                          static_cast<std::size_t>(depth));
            const auto ask_count = depth <= 0 ? 0 : std::min(expected.asks.size(),
                                                          static_cast<std::size_t>(depth));
            const std::vector<Level> bids(expected.bids.begin(), expected.bids.begin() + bid_count);
            const std::vector<Level> asks(expected.asks.begin(), expected.asks.begin() + ask_count);
            require(book.get_bids(depth) == bids, std::string(name) + ": bid depth mismatch");
            require(book.get_asks(depth) == asks, std::string(name) + ": ask depth mismatch");
        }
    }
}

template <typename Book>
void edge_cases(const char* name) {
    Book book;
    const BBO empty;
    require(book.get_bbo() == empty, "initial book not empty");
    require(!book.cancel_order(42), "unknown cancellation succeeded");
    require(book.get_bids().empty() && book.get_asks().empty(), "empty depth not empty");

    book.add_order(101, Side::Buy, 9950, 10);
    book.add_order(102, Side::Buy, 9950, 20);
    book.add_order(103, Side::Buy, 9975, 5);
    require(book.get_bbo() == BBO{true, 9975, 5, false, 0, 0}, "bid-only BBO");
    book.add_order(201, Side::Sell, 10025, 12);
    book.add_order(202, Side::Sell, 10050, 8);
    const View expected{{true, 9975, 5, true, 10025, 12},
                        {{9975, 5}, {9950, 30}}, {{10025, 12}, {10050, 8}}};
    check(book, expected, name, true);

    auto rejects = [&](OrderId id, Side side, Price price, Quantity qty) {
        bool rejected = false;
        try {
            book.add_order(id, side, price, qty);
        } catch (const std::invalid_argument&) {
            rejected = true;
        }
        require(rejected, "invalid add accepted");
        check(book, expected, name, false); // Rejected adds must leave the book unchanged.
    };
    rejects(101, Side::Buy, 9900, 1);
    rejects(999, Side::Buy, 10000, 1);
    rejects(999, Side::Buy, 4999, 1);
    rejects(999, Side::Sell, 10000, 1);
    rejects(999, Side::Sell, 15001, 1);
    rejects(999, Side::Sell, 10001, 0);
    rejects(999, Side::Buy, 9999, -1);
    rejects(999, static_cast<Side>(99), 9999, 1);

    require(book.cancel_order(102), "cancel shared level");
    require(book.get_bids() == std::vector<Level>{{9975, 5}, {9950, 10}}, "shared level total");
    require(book.cancel_order(103), "cancel best bid");
    require(!book.cancel_order(103), "repeated cancel succeeded");
    require(book.get_bbo().bid_price == 9950, "best bid did not move");
    require(book.cancel_order(101), "cancel last bid");
    require(book.get_bbo() == BBO{false, 0, 0, true, 10025, 12}, "ask-only BBO");
    require(book.cancel_order(201), "cancel best ask");
    require(book.get_bbo().ask_price == 10050, "best ask did not move");
    require(book.cancel_order(202), "cancel last ask");
    require(book.get_bbo() == empty, "book not empty after cancellations");

    // Full price-band gaps, arbitrary IDs, and removing an order moved by swap-and-pop.
    const auto large_id = std::numeric_limits<OrderId>::max();
    book.add_order(0, Side::Buy, bid_min, 1);
    book.add_order(large_id, Side::Sell, ask_max, 2);
    book.add_order(17, Side::Buy, bid_max, 3);
    book.add_order(9000000000ULL, Side::Sell, ask_min, 4);
    require(book.cancel_order(17), "cancel sparse best bid");
    require(book.cancel_order(9000000000ULL), "cancel moved order");
    require(book.get_bbo() == BBO{true, bid_min, 1, true, ask_max, 2}, "sparse BBO");
    require(book.cancel_order(0) && book.cancel_order(large_id), "cancel boundary IDs");
    require(book.get_bbo() == empty, "boundary book not empty");

    const Quantity maximum = std::numeric_limits<Quantity>::max();
    book.add_order(301, Side::Buy, bid_min, maximum);
    bool overflow = false;
    try {
        book.add_order(302, Side::Buy, bid_min, 1);
    } catch (const std::overflow_error&) {
        overflow = true;
    }
    require(overflow, "quantity overflow accepted");
    require(!book.cancel_order(302), "overflowing order was stored");
    require(book.get_bbo().bid_qty == maximum, "overflow changed quantity");
    require(book.cancel_order(301), "cancel maximum quantity");

    std::cout << "Implementation " << name << " edge cases: PASS\n";
}

} // namespace

void verify(std::size_t operations, std::uint32_t seed) {
    edge_cases<MapBook>("A");
    edge_cases<VectorBook>("B");
    edge_cases<ArrayBook>("C");

    MapBook a;
    VectorBook b;
    ArrayBook c;
    ReferenceBook reference;
    const auto workload = make_workload(operations, seed);
    std::cout << "Checking all levels after each of " << operations
              << " random operations (seed " << seed << ")..." << std::endl;
    for (std::size_t i = 0; i < workload.size(); ++i) {
        const auto& op = workload[i];
        try {
            if (op.type == OpType::Add) {
                a.add_order(op.id, op.side, op.price, op.qty);
                b.add_order(op.id, op.side, op.price, op.qty);
                c.add_order(op.id, op.side, op.price, op.qty);
                reference.add_order(op.id, op.side, op.price, op.qty);
            } else if (op.type == OpType::Cancel) {
                const bool expected = reference.cancel_order(op.id);
                require(expected, "workload tried to cancel a non-live order");
                require(a.cancel_order(op.id) == expected, "A: cancel mismatch");
                require(b.cancel_order(op.id) == expected, "B: cancel mismatch");
                require(c.cancel_order(op.id) == expected, "C: cancel mismatch");
            }
            const auto expected = reference.snapshot();
            const bool depth = (i + 1) % 100 == 0;
            check(a, expected, "A", depth);
            check(b, expected, "B", depth);
            check(c, expected, "C", depth);
        } catch (const std::exception& error) {
            throw std::runtime_error("seed " + std::to_string(seed) + ", operation " +
                std::to_string(i + 1) + ", ID " + std::to_string(op.id) + ": " + error.what());
        }
    }

    // Drain in reverse insertion order to exercise best-price changes and empty sides.
    for (auto it = workload.rbegin(); it != workload.rend(); ++it) {
        if (it->type != OpType::Add) continue;
        const bool expected = reference.cancel_order(it->id);
        require(a.cancel_order(it->id) == expected, "A: drain cancel mismatch");
        require(b.cancel_order(it->id) == expected, "B: drain cancel mismatch");
        require(c.cancel_order(it->id) == expected, "C: drain cancel mismatch");
        const auto state = reference.snapshot();
        check(a, state, "A", false);
        check(b, state, "B", false);
        check(c, state, "C", false);
    }
    require(a.get_bbo() == BBO{} && b.get_bbo() == BBO{} && c.get_bbo() == BBO{},
            "drained book not empty");
    std::cout << "All implementations match the independent reference: PASS\n";
}

} // namespace ob


#include "map_book.hpp"
#include "heap_book.hpp"
#include "array_book.hpp"
#include "segment_tree_book.hpp"

#include <array>
#include <cstdlib>
#include <iostream>
#include <random>
#include <string>
#include <vector>

using namespace orderbook;

namespace {

struct NamedBook {
    const char* name;
    OrderBook* book;
};

using Books = std::array<NamedBook, 4>;

void fail(const std::string& message) {
    std::cerr << "FAILED: " << message << '\n';
    std::exit(EXIT_FAILURE);
}

void check(bool condition, const std::string& message) {
    if (!condition) {
        fail(message);
    }
}

bool same_bbo(const BBO& a, const BBO& b) {
    return a.has_bid == b.has_bid &&
           a.bid_price == b.bid_price &&
           a.bid_qty == b.bid_qty &&
           a.has_ask == b.has_ask &&
           a.ask_price == b.ask_price &&
           a.ask_qty == b.ask_qty;
}

bool same_levels(
    const std::vector<Level>& a,
    const std::vector<Level>& b
) {
    if (a.size() != b.size()) {
        return false;
    }

    for (std::size_t i = 0; i < a.size(); ++i) {
        if (a[i].price != b[i].price ||
            a[i].qty != b[i].qty) {
            return false;
        }
    }

    return true;
}

void compare_state(const Books& books, bool compare_depth) {
    const OrderBook& reference = *books[0].book;
    const BBO reference_bbo = reference.get_bbo();

    for (std::size_t i = 1; i < books.size(); ++i) {
        const BBO current_bbo = books[i].book->get_bbo();

        if (!same_bbo(reference_bbo, current_bbo)) {
            fail(
                std::string(books[0].name) + "/" +
                books[i].name + " BBO mismatch"
            );
        }

        if (compare_depth) {
            if (!same_levels(
                    reference.get_bids(10),
                    books[i].book->get_bids(10))) {
                fail(
                    std::string(books[0].name) + "/" +
                    books[i].name + " bid depth mismatch"
                );
            }

            if (!same_levels(
                    reference.get_asks(10),
                    books[i].book->get_asks(10))) {
                fail(
                    std::string(books[0].name) + "/" +
                    books[i].name + " ask depth mismatch"
                );
            }
        }
    }
}

void add_all(
    const Books& books,
    OrderId id,
    Side side,
    Price price,
    Quantity qty,
    bool expected
) {
    for (const NamedBook& entry : books) {
        const bool actual =
            entry.book->add_order(id, side, price, qty);

        if (actual != expected) {
            std::cerr
                << entry.name << " add mismatch: id=" << id
                << ", side=" << (side == Side::Buy ? "Buy" : "Sell")
                << ", price=" << price
                << ", qty=" << qty
                << ", expected=" << expected
                << ", actual=" << actual << '\n';

            std::exit(EXIT_FAILURE);
        }
    }

    compare_state(books, true);
}

void cancel_all(
    const Books& books,
    OrderId id,
    bool expected
) {
    for (const NamedBook& entry : books) {
        const bool actual = entry.book->cancel_order(id);

        if (actual != expected) {
            std::cerr
                << entry.name << " cancel mismatch: id=" << id
                << ", expected=" << expected
                << ", actual=" << actual << '\n';

            std::exit(EXIT_FAILURE);
        }
    }

    compare_state(books, true);
}

void basic_tests() {
    MapOrderBook a;
    HeapOrderBook b;
    ArrayOrderBook c;
    SegmentTreeOrderBook d;

    const Books books{{
        {"A", &a},
        {"B", &b},
        {"C", &c},
        {"D", &d}
    }};

    // Multiple orders at the same price must aggregate quantities.
    add_all(books, 1, Side::Buy, 9900, 10, true);
    add_all(books, 2, Side::Buy, 9900, 20, true);
    add_all(books, 3, Side::Sell, 10100, 5, true);

    BBO bbo = d.get_bbo();

    check(
        bbo.has_bid && bbo.bid_price == 9900 && bbo.bid_qty == 30,
        "Bid aggregation failed"
    );

    check(
        bbo.has_ask && bbo.ask_price == 10100 && bbo.ask_qty == 5,
        "Ask aggregation failed"
    );

    // Cancelling one order must preserve the remaining quantity.
    cancel_all(books, 1, true);

    check(
        d.get_bbo().has_bid && d.get_bbo().bid_qty == 20,
        "Partial cancellation failed"
    );

    // Removing the final order at the best bid removes that level.
    cancel_all(books, 2, true);

    check(!d.get_bbo().has_bid, "Empty bid side should have no BBO");

    // Recreating a level checks heap cleanup and segment-tree updates.
    add_all(books, 4, Side::Buy, 9900, 7, true);

    check(
        d.get_bbo().has_bid && d.get_bbo().bid_qty == 7,
        "Recreated bid level failed"
    );

    // Invalid IDs, prices, and quantities must be rejected.
    add_all(books, 0, Side::Buy, 9900, 1, false);
    add_all(books, 5, Side::Buy, 10000, 1, false);
    add_all(books, 6, Side::Sell, 10000, 1, false);
    add_all(books, 7, Side::Buy, 9900, 0, false);

    // Duplicate active IDs must be rejected.
    add_all(books, 4, Side::Buy, 9800, 1, false);

    // Cancelling a nonexistent ID must fail.
    cancel_all(books, 999999, false);

    // IDs remain reserved after cancellation.
    cancel_all(books, 4, true);
    add_all(books, 4, Side::Buy, 9900, 1, false);

    std::cout << "Basic correctness tests passed for A, B, C and D.\n";
}

void randomized_differential_test() {
    MapOrderBook a;
    HeapOrderBook b;
    ArrayOrderBook c;
    SegmentTreeOrderBook d;

    const Books books{{
        {"A", &a},
        {"B", &b},
        {"C", &c},
        {"D", &d}
    }};

    std::mt19937_64 rng(12648430);

    std::vector<OrderId> live_ids;
    live_ids.reserve(8000);

    OrderId next_id = 100;

    for (int i = 1; i <= 8000; ++i) {
        const int action = static_cast<int>(rng() % 100);

        if (action < 65 || live_ids.empty()) {
            const Side side = (rng() % 2 == 0)
                ? Side::Buy
                : Side::Sell;

            const Price price = side == Side::Buy
                ? 5000 + static_cast<Price>(rng() % 5000)
                : 10001 + static_cast<Price>(rng() % 5000);

            const Quantity qty =
                1 + static_cast<Quantity>(rng() % 100);

            const OrderId id = next_id++;

            add_all(books, id, side, price, qty, true);
            live_ids.push_back(id);

        } else if (action < 85) {
            const std::size_t index =
                static_cast<std::size_t>(rng() % live_ids.size());

            const OrderId id = live_ids[index];

            cancel_all(books, id, true);

            live_ids[index] = live_ids.back();
            live_ids.pop_back();

        } else {
            compare_state(books, true);
        }

        // Compare BBO after every operation and depth periodically.
        compare_state(books, i % 113 == 0);
    }

    std::cout
        << "Differential test passed: 8000 deterministic operations "
        << "compared across A, B, C and D.\n";
}

} // namespace

int main() {
    basic_tests();
    randomized_differential_test();

    std::cout << "All correctness tests passed.\n";
    return 0;
}


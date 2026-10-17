#include "verification.hpp"
#include "implementation/array_order_book.hpp"
#include "implementation/map_order_book.hpp"
#include "implementation/vector_order_book.hpp"
#include <algorithm>
#include <iostream>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace {
void require(bool condition, const std::string &message) {
    if (!condition) {
        throw std::runtime_error("verification: " + message);
    }
}
bool same(const BBO &a, const BBO &b) {
    return a.has_bid == b.has_bid && a.bid_price_ticks == b.bid_price_ticks &&
           a.bid_qty == b.bid_qty && a.has_ask == b.has_ask &&
           a.ask_price_ticks == b.ask_price_ticks && a.ask_qty == b.ask_qty;
}
bool same(const std::vector<Level> &a, const std::vector<Level> &b) {
    return a.size() == b.size() &&
           std::equal(a.begin(), a.end(), b.begin(), [](const Level &x, const Level &y) {
               return x.price_ticks == y.price_ticks && x.qty == y.qty;
           });
}

struct Books {
    MapOrderBook a;
    VectorOrderBook b;
    ArrayOrderBook c;
    void check(int depth = 5000) const {
        require(same(a.get_bbo(), b.get_bbo()) && same(a.get_bbo(), c.get_bbo()), "BBO differs");
        const auto bids = a.get_bids(depth);
        const auto asks = a.get_asks(depth);
        require(same(bids, b.get_bids(depth)) && same(bids, c.get_bids(depth)),
                "bid levels differ");
        require(same(asks, b.get_asks(depth)) && same(asks, c.get_asks(depth)),
                "ask levels differ");
    }
    void add(std::uint64_t id, Side side, int price, long qty) {
        a.add_order(id, side, price, qty);
        b.add_order(id, side, price, qty);
        c.add_order(id, side, price, qty);
    }
    void cancel(std::uint64_t id, bool expected) {
        require(a.cancel_order(id) == expected, "A cancel result");
        require(b.cancel_order(id) == expected, "B cancel result");
        require(c.cancel_order(id) == expected, "C cancel result");
    }
};

// Independent small oracle: store only orders, recompute all levels by scanning.
// This avoids treating agreement between implementations as the only evidence.
void oracle_check(const Books &books, const std::unordered_map<std::uint64_t, Order> &orders) {
    std::map<int, long> bid_totals, ask_totals;
    for (const auto &item : orders) {
        const auto &o = item.second;
        (o.side == Side::Buy ? bid_totals : ask_totals)[o.price_ticks] += o.qty;
    }
    std::vector<Level> bids, asks;
    for (auto it = bid_totals.rbegin(); it != bid_totals.rend(); ++it) {
        bids.push_back({it->first, it->second});
    }
    for (const auto &level : ask_totals) {
        asks.push_back({level.first, level.second});
    }
    BBO expected;
    if (!bids.empty()) {
        expected.has_bid = true;
        expected.bid_price_ticks = bids.front().price_ticks;
        expected.bid_qty = bids.front().qty;
    }
    if (!asks.empty()) {
        expected.has_ask = true;
        expected.ask_price_ticks = asks.front().price_ticks;
        expected.ask_qty = asks.front().qty;
    }
    require(same(books.a.get_bbo(), expected), "oracle BBO");
    require(same(books.a.get_bids(5000), bids), "oracle bids");
    require(same(books.a.get_asks(5000), asks), "oracle asks");
    books.check();
}

void edge_cases() {
    Books books;
    books.check();
    require(!books.a.get_bbo().has_bid && !books.a.get_bbo().has_ask, "empty flags");
    books.cancel(0, false);
    books.cancel(std::numeric_limits<std::uint64_t>::max(), false);
    const long large = static_cast<long>(std::numeric_limits<int>::max());
    books.add(0, Side::Buy, 9999, large);
    books.add(1, Side::Buy, 9999, 100);
    books.add(2, Side::Buy, 5000, 5);
    books.add(std::numeric_limits<std::uint64_t>::max(), Side::Sell, 10001, 9);
    books.add(3, Side::Sell, 15000, 6);
    books.check();
    require(books.a.get_bbo().bid_qty == large + 100, "quantity accumulation");
    require(books.a.get_bids(10).size() == 2 && books.a.get_asks(10).size() == 2,
            "insufficient depth");
    for (int depth : {-10, 0, 1, 2, 10, 5000, std::numeric_limits<int>::max()}) {
        books.check(depth);
        if (depth <= 0) {
            require(books.a.get_bids(depth).empty() && books.a.get_asks(depth).empty(),
                    "nonpositive depth");
        }
    }
    books.cancel(0, true);
    books.check();
    require(books.a.get_bbo().bid_qty == 100, "partial cancellation quantity");
    books.cancel(0, false);
    books.cancel(1, true);
    books.check();
    require(books.a.get_bbo().bid_price_ticks == 5000, "best bid changes");
    books.cancel(std::numeric_limits<std::uint64_t>::max(), true);
    books.check();
    require(books.a.get_bbo().ask_price_ticks == 15000, "best ask changes");
    books.cancel(2, true);
    books.check();
    require(!books.a.get_bbo().has_bid && books.a.get_bbo().has_ask, "one-sided book");
    books.cancel(3, true);
    books.check();

    // Visit every price, bitmap boundary and partial last bitmap word.
    Books sweep;
    for (int i = 0; i < 5000; ++i) {
        sweep.add(static_cast<std::uint64_t>(i) * 2, Side::Buy, 5000 + i, 1);
        sweep.check(i % 79 == 0 ? 5000 : 10);
        sweep.add(static_cast<std::uint64_t>(i) * 2 + 1, Side::Sell, 10001 + i, 2);
        sweep.check(i % 79 == 0 ? 5000 : 10);
    }
    for (int i = 0; i < 5000; ++i) {
        sweep.cancel(static_cast<std::uint64_t>(4999 - i) * 2, true);
        sweep.check(i % 79 == 0 ? 5000 : 10);
        sweep.cancel(static_cast<std::uint64_t>(i) * 2 + 1, true);
        sweep.check(i % 79 == 0 ? 5000 : 10);
    }
    sweep.check();
    require(!sweep.a.get_bbo().has_bid && !sweep.a.get_bbo().has_ask, "drain all levels");

    // Repeated turnover forces tombstone reclamation while occupancy stays low.
    Books churn;
    for (std::uint64_t id = 100; id < 10100; ++id) {
        const auto wide_id = id * 0x100000001ULL;
        churn.add(wide_id, Side::Buy, 5000 + static_cast<int>(id % 5000), 3);
        churn.check();
        churn.cancel(wide_id, true);
        churn.check();
        churn.cancel(wide_id, false);
    }
}
} // namespace

void verify(const Workload &workload) {
    std::cout << "Running edge cases, all-price sweep and tombstone churn...\n";
    edge_cases();
    Books books;
    std::unordered_map<std::uint64_t, Order> oracle;
    std::cout << "Comparing BBO and ALL active levels after every random operation...\n";
    for (std::size_t i = 0; i < workload.operations.size(); ++i) {
        const auto &op = workload.operations[i];
        try {
            if (op.kind == Kind::Add) {
                books.add(op.id, op.side, op.price_ticks, op.qty);
                if (i < 1000) {
                    oracle.emplace(op.id, Order{op.side, op.price_ticks, op.qty});
                }
            } else if (op.kind == Kind::Cancel) {
                books.cancel(op.id, true);
                if (i < 1000) {
                    require(oracle.erase(op.id) == 1, "workload cancel not live");
                }
            }
            books.check();
            if ((i + 1) % 100 == 0) {
                books.check(10);
            }
            if (i < 1000) {
                oracle_check(books, oracle);
            }
        } catch (const std::exception &e) {
            throw std::runtime_error("operation " + std::to_string(i + 1) + ": " + e.what());
        }
    }
    std::cout << "Implementation A: PASS\nImplementation B: PASS\nImplementation C: PASS\n"
                 "Independent recomputing oracle (first 1000 operations): PASS\n"
                 "All implementations produce identical results.\n";
}

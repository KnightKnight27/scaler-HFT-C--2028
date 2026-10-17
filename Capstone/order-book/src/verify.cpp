#include "verify.hpp"

#include <cstdio>
#include <limits>
#include <map>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

#include "array_order_book.hpp"
#include "map_order_book.hpp"
#include "order_table.hpp"
#include "sorted_vector_order_book.hpp"

namespace {

// Every this many operations the random tests compare all price levels, not just the top 10.
constexpr std::size_t kFullCompareEvery = 10000;

// ---------------------------------------------------------------------------
// Comparing and describing results

bool same_bbo(const BBO& a, const BBO& b) {
    if (a.has_bid != b.has_bid || a.has_ask != b.has_ask) return false;
    if (a.has_bid && (a.bid_price != b.bid_price || a.bid_qty != b.bid_qty)) return false;
    if (a.has_ask && (a.ask_price != b.ask_price || a.ask_qty != b.ask_qty)) return false;
    return true;
}

bool same_levels(const std::vector<Level>& a, const std::vector<Level>& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (a[i].price != b[i].price || a[i].qty != b[i].qty) return false;
    }
    return true;
}

std::string describe(const BBO& bbo) {
    char bid[64] = "none";
    char ask[64] = "none";
    if (bbo.has_bid) std::snprintf(bid, sizeof bid, "%.2f x %ld", bbo.bid_price, bbo.bid_qty);
    if (bbo.has_ask) std::snprintf(ask, sizeof ask, "%.2f x %ld", bbo.ask_price, bbo.ask_qty);
    return std::string("bid ") + bid + ", ask " + ask;
}

std::string describe(const Op& op) {
    char text[96];
    switch (op.type) {
    case OpType::Add:
        std::snprintf(text, sizeof text, "add id %llu %s %.2f x %u", static_cast<unsigned long long>(op.id),
                      op.side == Side::Buy ? "buy" : "sell", op.price, op.qty);
        break;
    case OpType::Cancel:
        std::snprintf(text, sizeof text, "cancel id %llu", static_cast<unsigned long long>(op.id));
        break;
    case OpType::Bbo:
        std::snprintf(text, sizeof text, "get_bbo");
        break;
    }
    return text;
}

// Explains the first difference between two level lists.
std::string level_difference(const std::vector<Level>& got, const std::vector<Level>& want) {
    char text[160];
    for (std::size_t i = 0; i < got.size() && i < want.size(); ++i) {
        if (got[i].price != want[i].price || got[i].qty != want[i].qty) {
            std::snprintf(text, sizeof text, "level %zu is %.2f x %ld, expected %.2f x %ld", i, got[i].price,
                          got[i].qty, want[i].price, want[i].qty);
            return text;
        }
    }
    std::snprintf(text, sizeof text, "%zu levels, expected %zu", got.size(), want.size());
    return text;
}

// ---------------------------------------------------------------------------
// Reference book

// Deliberately simple book used only as the reference in the random tests:
// an array of total quantity per tick, scanned from the inside out on every query.
class ReferenceBook {
public:
    bool add_order(std::uint64_t id, Side side, double price, long qty) {
        Tick tick = 0;
        if (!validate_order(side, price, qty, tick) || orders_.count(id) != 0) return false;
        orders_[id] = Order{tick, qty};
        qty_at_[tick] += qty;
        return true;
    }

    bool cancel_order(std::uint64_t id) {
        const auto it = orders_.find(id);
        if (it == orders_.end()) return false;
        qty_at_[it->second.tick] -= it->second.qty;
        orders_.erase(it);
        return true;
    }

    BBO get_bbo() const {
        BBO bbo{};
        const std::vector<Level> bid = get_bids(1);
        const std::vector<Level> ask = get_asks(1);
        if (!bid.empty()) {
            bbo.has_bid = true;
            bbo.bid_price = bid[0].price;
            bbo.bid_qty = bid[0].qty;
        }
        if (!ask.empty()) {
            bbo.has_ask = true;
            bbo.ask_price = ask[0].price;
            bbo.ask_qty = ask[0].qty;
        }
        return bbo;
    }

    std::vector<Level> get_bids(int depth) const {
        std::vector<Level> out;
        for (Tick t = kMaxBidTick; t >= kMinBidTick && static_cast<int>(out.size()) < depth; --t) {
            if (qty_at_[t] != 0) out.push_back({tick_to_price(t), qty_at_[t]});
        }
        return out;
    }

    std::vector<Level> get_asks(int depth) const {
        std::vector<Level> out;
        for (Tick t = kMinAskTick; t <= kMaxAskTick && static_cast<int>(out.size()) < depth; ++t) {
            if (qty_at_[t] != 0) out.push_back({tick_to_price(t), qty_at_[t]});
        }
        return out;
    }

private:
    struct Order {
        Tick tick;
        long qty;
    };

    std::map<std::uint64_t, Order> orders_;
    std::vector<long> qty_at_ = std::vector<long>(kMaxAskTick + 1, 0);
};

// ---------------------------------------------------------------------------
// Edge cases

// Counts the edge-case checks for one implementation and prints any failure.
class Checker {
public:
    explicit Checker(const char* name) : name_(name) {}

    void check(bool ok, const char* what) {
        ++checks_;
        if (!ok) {
            ++failures_;
            std::printf("   FAIL [%s] %s\n", name_, what);
        }
    }

    const char* name() const { return name_; }
    int checks() const { return checks_; }
    int failures() const { return failures_; }

private:
    const char* name_;
    int checks_ = 0;
    int failures_ = 0;
};

template <class Book>
void edge_case_tests(Checker& c) {
    const BBO empty{false, 0, 0, false, 0, 0};

    {  // Empty book
        Book book;
        c.check(same_bbo(book.get_bbo(), empty), "empty book: get_bbo() has no bid and no ask");
        c.check(book.get_bids(10).empty() && book.get_asks(10).empty(), "empty book: depth queries return nothing");
        c.check(!book.cancel_order(1), "empty book: cancel_order() returns false");
    }

    {  // Orders at one price add up, and a cancel removes exactly one order
        Book book;
        c.check(book.add_order(1, Side::Buy, 99.50, 10), "add a bid");
        c.check(book.add_order(2, Side::Buy, 99.50, 5), "add a second bid at the same price");
        c.check(book.add_order(3, Side::Sell, 100.25, 7), "add an ask");
        c.check(same_bbo(book.get_bbo(), BBO{true, 99.50, 15, true, 100.25, 7}),
                "a level's quantity is the sum of its orders");
        c.check(book.cancel_order(1), "cancelling a live order returns true");
        c.check(same_bbo(book.get_bbo(), BBO{true, 99.50, 5, true, 100.25, 7}),
                "a cancel takes that order's quantity off its level");
        c.check(!book.cancel_order(1), "cancelling the same order again returns false");
        c.check(!book.cancel_order(12345), "cancelling an id that never existed returns false");
        c.check(same_bbo(book.get_bbo(), BBO{true, 99.50, 5, true, 100.25, 7}),
                "failed cancels leave the book unchanged");
        c.check(book.cancel_order(2) && same_bbo(book.get_bbo(), BBO{false, 0, 0, true, 100.25, 7}),
                "cancelling the last bid leaves no bid");
        c.check(book.cancel_order(3) && same_bbo(book.get_bbo(), empty), "cancelling every order empties the book");
    }

    {  // Depth: best price first, and fewer levels than requested
        Book book;
        book.add_order(1, Side::Buy, 99.00, 1);
        book.add_order(2, Side::Buy, 99.10, 2);
        book.add_order(3, Side::Buy, 98.90, 3);
        book.add_order(4, Side::Sell, 100.30, 4);
        book.add_order(5, Side::Sell, 100.10, 5);
        book.add_order(6, Side::Sell, 100.20, 6);
        c.check(same_levels(book.get_bids(10), {{99.10, 2}, {99.00, 1}, {98.90, 3}}),
                "get_bids(10) with 3 bid levels returns those 3, highest first");
        c.check(same_levels(book.get_asks(10), {{100.10, 5}, {100.20, 6}, {100.30, 4}}),
                "get_asks(10) with 3 ask levels returns those 3, lowest first");
        c.check(same_levels(book.get_bids(2), {{99.10, 2}, {99.00, 1}}), "get_bids(2) returns only the best 2 levels");
        c.check(book.get_bids(0).empty() && book.get_asks(-5).empty(), "depth 0 or below returns no levels");
    }

    {  // The best price moves to the next non-empty level when the best level empties
        Book book;
        c.check(book.add_order(1, Side::Buy, 99.99, 1) && book.add_order(2, Side::Buy, 50.00, 2) &&
                    book.add_order(3, Side::Sell, 100.01, 3) && book.add_order(4, Side::Sell, 150.00, 4),
                "orders at the edges of both price bands are accepted");
        c.check(same_bbo(book.get_bbo(), BBO{true, 99.99, 1, true, 100.01, 3}), "BBO uses the inner prices");
        book.cancel_order(1);
        book.cancel_order(3);
        c.check(same_bbo(book.get_bbo(), BBO{true, 50.00, 2, true, 150.00, 4}),
                "best price skips 4998 empty levels to the next order");
    }

    {  // Invalid input is rejected and leaves the book unchanged
        Book book;
        c.check(!book.add_order(1, Side::Buy, 100.00, 10), "bid at the mid price 100.00 is rejected");
        c.check(!book.add_order(2, Side::Sell, 100.00, 10), "ask at the mid price 100.00 is rejected");
        c.check(!book.add_order(3, Side::Buy, 49.99, 10), "bid below 50.00 is rejected");
        c.check(!book.add_order(4, Side::Sell, 150.01, 10), "ask above 150.00 is rejected");
        c.check(!book.add_order(5, Side::Buy, 100.50, 10), "bid in the ask band is rejected");
        c.check(!book.add_order(6, Side::Sell, 99.50, 10), "ask in the bid band is rejected");
        c.check(!book.add_order(7, Side::Buy, 99.505, 10), "price that is not a multiple of 0.01 is rejected");
        c.check(!book.add_order(8, Side::Buy, 99.50, 0), "zero quantity is rejected");
        c.check(!book.add_order(9, Side::Buy, 99.50, -5), "negative quantity is rejected");
        if (std::numeric_limits<long>::max() > kMaxOrderQty) {
            c.check(!book.add_order(10, Side::Buy, 99.50, kMaxOrderQty + 1), "quantity above 2^31 - 1 is rejected");
        }
        c.check(!book.add_order(11, Side::Buy, std::numeric_limits<double>::quiet_NaN(), 10), "NaN price is rejected");
        c.check(!book.add_order(12, Side::Sell, std::numeric_limits<double>::infinity(), 10),
                "infinite price is rejected");
        c.check(same_bbo(book.get_bbo(), empty), "rejected orders leave the book empty");
        c.check(book.add_order(20, Side::Buy, 99.50, 10), "a valid order is accepted");
        c.check(!book.add_order(20, Side::Sell, 101.00, 5), "a new order with the id of a live order is rejected");
        c.check(same_bbo(book.get_bbo(), BBO{true, 99.50, 10, false, 0, 0}),
                "the rejected duplicate did not change the book");
    }

    {  // Unusual ids: 0, the largest uint64_t, ids arriving out of order, re-using a cancelled id
        Book book;
        const std::uint64_t max_id = std::numeric_limits<std::uint64_t>::max();
        c.check(book.add_order(max_id, Side::Buy, 60.00, 1), "id 2^64 - 1 is accepted");
        c.check(book.add_order(0, Side::Buy, 60.00, 2), "id 0 is accepted after a larger id");
        c.check(book.add_order(500, Side::Sell, 140.00, 3), "an id between two earlier ids is accepted");
        c.check(same_bbo(book.get_bbo(), BBO{true, 60.00, 3, true, 140.00, 3}), "those orders are all in the book");
        c.check(book.cancel_order(0) && book.cancel_order(max_id), "orders with unusual ids can be cancelled");
        c.check(!book.cancel_order(0) && !book.cancel_order(max_id), "and cannot be cancelled twice");
        c.check(book.cancel_order(500) && book.add_order(500, Side::Sell, 140.00, 4),
                "the id of a cancelled order can be used again");
        c.check(same_bbo(book.get_bbo(), BBO{false, 0, 0, true, 140.00, 4}), "the re-used id holds the new order");
    }

    {  // Fill every price level, then empty the book from the best price outwards,
       // so the best price has to step through all 5000 levels on each side.
        Book book;
        std::uint64_t id = 1;
        for (Tick t = kMinBidTick; t <= kMaxBidTick; ++t) book.add_order(id++, Side::Buy, tick_to_price(t), 1);
        for (Tick t = kMinAskTick; t <= kMaxAskTick; ++t) book.add_order(id++, Side::Sell, tick_to_price(t), 2);
        const std::size_t per_side = kLevelsPerSide;
        c.check(book.get_bids(kLevelsPerSide).size() == per_side && book.get_asks(kLevelsPerSide + 1).size() == per_side,
                "all 10000 price levels can be filled and listed");
        c.check(same_bbo(book.get_bbo(), BBO{true, 99.99, 1, true, 100.01, 2}), "a full book has 99.99 / 100.01 as BBO");

        bool bids_ok = true;
        for (Tick t = kMaxBidTick; t >= kMinBidTick; --t) {
            book.cancel_order(static_cast<std::uint64_t>(t - kMinBidTick + 1));  // the bid at tick t
            const BBO bbo = book.get_bbo();
            const bool expect_bid = t > kMinBidTick;
            if (bbo.has_bid != expect_bid || (expect_bid && bbo.bid_price != tick_to_price(t - 1))) bids_ok = false;
        }
        c.check(bids_ok, "cancelling bids from the top moves the best bid down one tick at a time");

        bool asks_ok = true;
        for (Tick t = kMinAskTick; t <= kMaxAskTick; ++t) {
            book.cancel_order(static_cast<std::uint64_t>(kLevelsPerSide + t - kMinAskTick + 1));  // the ask at tick t
            const BBO bbo = book.get_bbo();
            const bool expect_ask = t < kMaxAskTick;
            if (bbo.has_ask != expect_ask || (expect_ask && bbo.ask_price != tick_to_price(t + 1))) asks_ok = false;
        }
        c.check(asks_ok, "cancelling asks from the bottom moves the best ask up one tick at a time");
        c.check(same_bbo(book.get_bbo(), empty) && book.get_bids(10).empty() && book.get_asks(10).empty(),
                "the book is empty again afterwards");
    }
}

// ---------------------------------------------------------------------------
// Hash table self-test

// Random inserts, finds and erases on C's OrderTable, checked against
// std::unordered_map. The table starts at 16 slots, so this covers growth,
// probe runs that wrap around the end of the array, and the backward-shift erase.
bool order_table_self_test(int& operations) {
    std::mt19937 rng(2026);
    operations = 0;
    for (const std::uint32_t key_range : {7u, 3000u}) {
        OrderTable table;
        std::unordered_map<std::uint64_t, OrderTable::Slot> expected;
        for (int step = 0; step < 200000; ++step, ++operations) {
            const std::uint64_t id = rng() % key_range;
            const std::uint32_t action = rng() % 3;  // 0 insert, 1 erase, 2 lookup only

            OrderTable::Slot* slot = table.find(id);
            const auto it = expected.find(id);
            if ((slot != nullptr) != (it != expected.end())) return false;
            if (slot != nullptr && (slot->tick != it->second.tick || slot->qty != it->second.qty)) return false;

            if (action == 0) {
                const Tick tick = kMinBidTick + static_cast<Tick>(rng() % 10001);
                const std::int32_t qty = 1 + static_cast<std::int32_t>(rng() % 100);
                const bool inserted = table.insert(id, tick, qty);
                if (inserted != (it == expected.end())) return false;
                if (inserted) expected[id] = OrderTable::Slot{id, tick, qty};
            } else if (action == 1 && slot != nullptr) {
                table.erase(slot);
                expected.erase(it);
            }
            if (table.size() != expected.size()) return false;
        }
        for (const auto& entry : expected) {
            const OrderTable::Slot* slot = table.find(entry.first);
            if (slot == nullptr || slot->tick != entry.second.tick || slot->qty != entry.second.qty) return false;
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// Random workloads in lockstep

// Outcome of the random tests for one implementation. Keeps the first mismatch.
struct Verdict {
    bool ok = true;
    std::string error;

    void fail(const std::string& message) {
        if (!ok) return;
        ok = false;
        error = message;
    }
};

struct Snapshot {
    BBO bbo;
    std::vector<Level> bids;
    std::vector<Level> asks;
};

template <class Book>
Snapshot take_snapshot(const Book& book, int depth) {
    return Snapshot{book.get_bbo(), book.get_bids(depth), book.get_asks(depth)};
}

// Returns an empty string when the snapshots match.
std::string snapshot_difference(const Snapshot& got, const Snapshot& want, int depth) {
    if (!same_bbo(got.bbo, want.bbo)) return "get_bbo() gave " + describe(got.bbo) + ", expected " + describe(want.bbo);
    if (!same_levels(got.bids, want.bids)) {
        return "get_bids(" + std::to_string(depth) + "): " + level_difference(got.bids, want.bids);
    }
    if (!same_levels(got.asks, want.asks)) {
        return "get_asks(" + std::to_string(depth) + "): " + level_difference(got.asks, want.asks);
    }
    return {};
}

template <class Book>
bool apply(Book& book, const Op& op) {
    switch (op.type) {
    case OpType::Add:
        return book.add_order(op.id, op.side, op.price, op.qty);
    case OpType::Cancel:
        return book.cancel_order(op.id);
    case OpType::Bbo:
        book.get_bbo();
        return true;
    }
    return false;
}

// Applies every operation to A, B, C and the reference book, then compares the
// return value, BBO and depth of each book with the reference.
void cross_check(const Workload& workload, Verdict& a_verdict, Verdict& b_verdict, Verdict& c_verdict) {
    MapOrderBook a;
    SortedVectorOrderBook b;
    ArrayOrderBook c;
    ReferenceBook reference;

    for (std::size_t i = 0; i < workload.ops.size(); ++i) {
        const Op& op = workload.ops[i];
        const bool want_result = apply(reference, op);
        const bool full = (i + 1) % kFullCompareEvery == 0 || i + 1 == workload.ops.size();
        const int depth = full ? kLevelsPerSide : kDepth;
        const Snapshot want = take_snapshot(reference, depth);

        auto check = [&](auto& book, Verdict& verdict) {
            const bool result = apply(book, op);
            if (!verdict.ok) return;  // already diverged, later differences add nothing
            std::string error;
            if (result != want_result) {
                error = std::string("returned ") + (result ? "true" : "false") + ", expected " +
                        (want_result ? "true" : "false");
            } else {
                error = snapshot_difference(take_snapshot(book, depth), want, depth);
            }
            if (!error.empty()) verdict.fail("operation " + std::to_string(i) + " (" + describe(op) + "): " + error);
        };
        check(a, a_verdict);
        check(b, b_verdict);
        check(c, c_verdict);
    }
}

void print_result(const char* name, bool ok, const Verdict& verdict) {
    std::printf("Implementation %s: %s\n", name, ok ? "PASS" : "FAIL");
    if (!verdict.ok) std::printf("  first mismatch at %s\n", verdict.error.c_str());
}

}  // namespace

bool run_verify(const WorkloadConfig& benchmark_config) {
    std::printf("Running correctness test...\n\n");

    std::printf("1. Edge cases: empty book, unknown and repeated cancels, short depth, invalid\n"
                "   input, unusual ids, every price level filled and then emptied\n");
    Checker a_checks("A");
    Checker b_checks("B");
    Checker c_checks("C");
    edge_case_tests<MapOrderBook>(a_checks);
    edge_case_tests<SortedVectorOrderBook>(b_checks);
    edge_case_tests<ArrayOrderBook>(c_checks);
    for (const Checker* checker : {&a_checks, &b_checks, &c_checks}) {
        std::printf("   %s: %d of %d checks passed\n", checker->name(), checker->checks() - checker->failures(),
                    checker->checks());
    }
    int table_operations = 0;
    const bool table_ok = order_table_self_test(table_operations);
    std::printf("   C's hash table against std::unordered_map: %s (%d random operations)\n",
                table_ok ? "PASS" : "FAIL", table_operations);

    std::printf("\n2. Random workloads, run through A, B, C and a brute-force reference book\n"
                "   (an array of quantities scanned on every query). After every operation the\n"
                "   return value, BBO and top %d bid and ask levels are compared; every %zu\n"
                "   operations and at the end, every price level on both sides.\n",
                kDepth, kFullCompareEvery);

    WorkloadConfig churn;
    churn.num_ops = 300000;
    churn.seed = 7;
    churn.add_percent = 50;
    churn.cancel_percent = 48;
    churn.cancel_all_at_end = true;

    struct Case {
        const char* name;
        const char* note;
        WorkloadConfig config;
    };
    const Case cases[] = {
        {"benchmark workload", "the workload used by the benchmark", benchmark_config},
        {"churn workload", "50% add / 48% cancel, then every remaining order cancelled: a thin book where\n"
                           "      levels are created and emptied all the time",
         churn},
    };

    Verdict a_verdict;
    Verdict b_verdict;
    Verdict c_verdict;
    for (const Case& test : cases) {
        const Workload workload = generate_workload(test.config);
        std::printf("   - %s, seed %u: %zu operations (%zu add, %zu cancel, %zu BBO)\n      %s\n", test.name,
                    test.config.seed, workload.ops.size(), workload.adds, workload.cancels, workload.bbo_queries,
                    test.note);
        std::fflush(stdout);
        cross_check(workload, a_verdict, b_verdict, c_verdict);
    }

    const bool a_ok = a_checks.failures() == 0 && a_verdict.ok;
    const bool b_ok = b_checks.failures() == 0 && b_verdict.ok;
    const bool c_ok = c_checks.failures() == 0 && table_ok && c_verdict.ok;

    std::printf("\n");
    print_result("A", a_ok, a_verdict);
    print_result("B", b_ok, b_verdict);
    print_result("C", c_ok, c_verdict);
    if (a_ok && b_ok && c_ok) {
        std::printf("\nAll implementations produce identical results.\n");
        return true;
    }
    std::printf("\nThe implementations do not agree. See the failures above.\n");
    return false;
}

#include "types.hpp"
#include "book_map.hpp"
#include "book_vec.hpp"
#include "book_array.hpp"
#include "workload.hpp"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>
#include <algorithm>

using namespace ob;
using Clock = std::chrono::steady_clock;

struct RunResult {
    double ns_add;
    double ns_cancel;
    double ns_bbo;
    double ns_depth;
    double total_ms;      // wall time of the whole op loop (includes timer overhead)
    long long checksum;
    BBO final_bbo;
};

template <class Book>
RunResult run_once(const std::vector<Op>& ops) {
    Book book;

    uint64_t add_ns = 0, cancel_ns = 0, bbo_ns = 0, depth_ns = 0;
    uint64_t add_n  = 0, cancel_n  = 0, bbo_n  = 0, depth_n  = 0;
    long long checksum = 0;
    BBO final_bbo{};

    auto run_start = Clock::now();

    for (const Op& op : ops) {
        auto t0 = Clock::now();
        switch (op.type) {
            case OpType::Add:
                book.add_order(op.id, op.side, op.price, op.qty);
                ++add_n;
                break;
            case OpType::Cancel:
                book.cancel_order(op.id);
                ++cancel_n;
                break;
            case OpType::Bbo: {
                BBO b = book.get_bbo();
                checksum += b.bid_qty + b.ask_qty;
                final_bbo = b;
                ++bbo_n;
                break;
            }
            case OpType::Depth: {
                auto bids = book.get_bids(10);
                auto asks = book.get_asks(10);
                for (auto& l : bids) checksum += l.qty;
                for (auto& l : asks) checksum += l.qty;
                ++depth_n;
                break;
            }
        }
        auto t1 = Clock::now();
        uint64_t dt = std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
        switch (op.type) {
            case OpType::Add:    add_ns    += dt; break;
            case OpType::Cancel: cancel_ns += dt; break;
            case OpType::Bbo:    bbo_ns    += dt; break;
            case OpType::Depth:  depth_ns  += dt; break;
        }
    }

    auto run_end = Clock::now();

    RunResult r{};
    r.ns_add    = add_n    ? (double)add_ns    / add_n    : 0.0;
    r.ns_cancel = cancel_n ? (double)cancel_ns / cancel_n : 0.0;
    r.ns_bbo    = bbo_n    ? (double)bbo_ns    / bbo_n    : 0.0;
    r.ns_depth  = depth_n  ? (double)depth_ns  / depth_n  : 0.0;
    r.total_ms  = std::chrono::duration<double, std::milli>(run_end - run_start).count();
    r.checksum  = checksum;
    r.final_bbo = final_bbo;
    return r;
}

static double median(std::vector<double> v) {
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

template <class Book>
void bench_impl(const std::vector<Op>& ops, int runs, const char* name) {
    std::vector<double> a, c, b, d, t;
    RunResult last{};

    for (int i = 0; i < runs; ++i) {
        last = run_once<Book>(ops);
        a.push_back(last.ns_add);
        c.push_back(last.ns_cancel);
        b.push_back(last.ns_bbo);
        d.push_back(last.ns_depth);
        t.push_back(last.total_ms);
    }

    std::printf("impl=%s runs=%d ops=%zu (median of runs)\n", name, runs, ops.size());
    std::printf("  total_ms  %.1f\n", median(t));
    std::printf("  ns/add    %.1f\n", median(a));
    std::printf("  ns/cancel %.1f\n", median(c));
    std::printf("  ns/bbo    %.1f\n", median(b));
    std::printf("  ns/depth  %.1f\n", median(d));
    std::printf("  final bid: has=%d px=%.2f qty=%lld\n",
                last.final_bbo.has_bid,
                to_price(last.final_bbo.bid_price),
                (long long)last.final_bbo.bid_qty);
    std::printf("  final ask: has=%d px=%.2f qty=%lld\n",
                last.final_bbo.has_ask,
                to_price(last.final_bbo.ask_price),
                (long long)last.final_bbo.ask_qty);
    std::printf("  checksum %lld\n", last.checksum);
}

// Explicit checks for the required behaviours the random workload never hits:
// empty book, unknown/double cancel, insufficient depth, best level emptying.
template <class Book>
bool edge_tests(const char* name) {
    Book b;
    bool ok = true;
    auto check = [&](bool cond, const char* what) {
        if (!cond) {
            std::printf("  [%s] FAILED: %s\n", name, what);
            ok = false;
        }
    };

    BBO e = b.get_bbo();
    check(!e.has_bid && !e.has_ask, "empty book BBO has no bid/ask");
    check(b.get_bids(10).empty() && b.get_asks(10).empty(), "empty book depth is empty");
    check(!b.cancel_order(12345), "cancel unknown id returns false");

    b.add_order(1, Side::Buy,  9000,  10);
    b.add_order(2, Side::Buy,  9001,  20);
    b.add_order(3, Side::Buy,  9002,  30);
    b.add_order(4, Side::Buy,  9002,   5);   // same price -> quantities add up
    b.add_order(5, Side::Sell, 10050,  7);

    std::vector<Level> bids = b.get_bids(10);
    check(bids.size() == 3, "get_bids(10) with 3 levels returns 3");
    check(bids.size() == 3 && bids[0].price == 9002 && bids[0].qty == 35,
          "best bid first and same-price qty aggregated");
    check(b.get_asks(10).size() == 1, "get_asks(10) with 1 level returns 1");

    check(b.cancel_order(2), "cancel live order returns true");
    check(!b.cancel_order(2), "cancel already-cancelled order returns false");

    // Empty the best bid level completely; BBO must move to the next level.
    check(b.cancel_order(3) && b.cancel_order(4), "cancel both orders at best bid");
    BBO m = b.get_bbo();
    check(m.has_bid && m.bid_price == 9000 && m.bid_qty == 10,
          "BBO moves to next level after best level emptied");

    check(b.cancel_order(1) && b.cancel_order(5), "cancel remaining orders");
    BBO z = b.get_bbo();
    check(!z.has_bid && !z.has_ask, "book is empty again after all cancels");

    std::printf("  edge cases %s: %s\n", name, ok ? "PASS" : "FAIL");
    return ok;
}

// Runs the same workload through two books and compares BBO, top-10 bids/asks
// and cancel return values after every single operation.
template <class BookA, class BookB>
bool verify_pair(const std::vector<Op>& ops, const char* nameA, const char* nameB) {
    BookA a;
    BookB b;

    for (size_t i = 0; i < ops.size(); ++i) {
        const Op& op = ops[i];

        switch (op.type) {
            case OpType::Add:
                a.add_order(op.id, op.side, op.price, op.qty);
                b.add_order(op.id, op.side, op.price, op.qty);
                break;
            case OpType::Cancel: {
                bool ra = a.cancel_order(op.id);
                bool rb = b.cancel_order(op.id);
                if (ra != rb) {
                    std::printf("  MISMATCH cancel return at op %zu (id=%llu): %s=%d %s=%d\n",
                                i, (unsigned long long)op.id, nameA, ra, nameB, rb);
                    return false;
                }
                break;
            }
            case OpType::Bbo:
            case OpType::Depth:
                break;   // state is compared below for every op anyway
        }

        if (!(a.get_bbo() == b.get_bbo())) {
            std::printf("  MISMATCH bbo after op %zu (type=%d id=%llu)\n",
                        i, (int)op.type, (unsigned long long)op.id);
            return false;
        }
        if (a.get_bids(10) != b.get_bids(10)) {
            std::printf("  MISMATCH get_bids(10) after op %zu (type=%d)\n", i, (int)op.type);
            return false;
        }
        if (a.get_asks(10) != b.get_asks(10)) {
            std::printf("  MISMATCH get_asks(10) after op %zu (type=%d)\n", i, (int)op.type);
            return false;
        }
    }

    std::printf("  workload %s vs %s: PASS (%zu ops)\n", nameA, nameB, ops.size());
    return true;
}

int main(int argc, char** argv) {
    std::string impl = "A";
    int runs = 5;
    uint32_t seed = 42;
    size_t n = 1'000'000;
    bool verify = false;

    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--impl") && i + 1 < argc) impl = argv[++i];
        else if (!std::strcmp(argv[i], "--runs") && i + 1 < argc) runs = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--seed") && i + 1 < argc) seed = (uint32_t)std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--verify")) verify = true;
    }
    if (runs < 1) runs = 1;

    if (verify) {
        std::printf("Running correctness test (seed=%u, %zu ops)...\n\n", seed, n);

        bool eA = edge_tests<BookMap>("A");
        bool eB = edge_tests<BookVec>("B");
        bool eC = edge_tests<BookArray>("C");

        auto ops = make_workload(n, seed);
        bool ab = verify_pair<BookMap, BookVec>(ops, "A", "B");
        bool ac = verify_pair<BookMap, BookArray>(ops, "A", "C");
        bool bc = verify_pair<BookVec, BookArray>(ops, "B", "C");

        bool okA = eA && ab && ac;
        bool okB = eB && ab && bc;
        bool okC = eC && ac && bc;

        std::printf("\nImplementation A: %s\n", okA ? "PASS" : "FAIL");
        std::printf("Implementation B: %s\n", okB ? "PASS" : "FAIL");
        std::printf("Implementation C: %s\n", okC ? "PASS" : "FAIL");

        bool all = okA && okB && okC;
        std::printf("\n%s\n", all ? "All implementations produce identical results."
                                  : "Implementations DISAGREE - see mismatches above.");
        return all ? 0 : 1;
    }

    auto ops = make_workload(n, seed);

    if (impl == "A") bench_impl<BookMap>(ops, runs, "A");
    else if (impl == "B") bench_impl<BookVec>(ops, runs, "B");
    else if (impl == "C") bench_impl<BookArray>(ops, runs, "C");
    else {
        std::fprintf(stderr, "unknown impl: %s (use A, B or C)\n", impl.c_str());
        return 1;
    }
    return 0;
}
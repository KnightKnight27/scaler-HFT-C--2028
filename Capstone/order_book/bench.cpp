#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <random>
#include <string>
#include <vector>

#include "array_book.hpp"
#include "common.hpp"
#include "heap_book.hpp"
#include "map_book.hpp"

using Clock = std::chrono::steady_clock;

enum class OpType { Add, Cancel, Bbo };

struct Op {
    OpType type;
    uint64_t id;
    Side side;
    Price price;
    long qty;
};

// The whole workload is built before any timing starts.
// rng() % n is used (not uniform_int_distribution) so the same seed gives the
// same ops with any standard library.
// price_span limits how many ticks next to the mid price are used; the
// benchmark uses the full band, --verify also uses a tiny span so that levels
// are emptied and refilled all the time.
std::vector<Op> make_workload(int n, uint32_t seed, int price_span = LEVELS_PER_SIDE,
                              int cancel_pct = 20) {
    std::mt19937 rng(seed);
    std::vector<Op> ops;
    ops.reserve(n);
    std::vector<uint64_t> live;
    uint64_t next_id = 1;
    int add_pct = 90 - cancel_pct;  // adds, then cancels, last 10% are BBO queries

    for (int i = 0; i < n; i++) {
        int r = rng() % 100;
        if (r >= add_pct && r < 90 && !live.empty()) {
            size_t k = rng() % live.size();
            ops.push_back({OpType::Cancel, live[k], Side::Buy, 0, 0});
            live[k] = live.back();
            live.pop_back();
        } else if (r >= 90) {
            ops.push_back({OpType::Bbo, 0, Side::Buy, 0, 0});
        } else {
            Side side = (rng() % 2 == 0) ? Side::Buy : Side::Sell;
            Price off = rng() % price_span;
            // Buys are counted down from 99.99, sells up from 100.01.
            Price price = (side == Side::Buy) ? BID_HIGH - off : ASK_LOW + off;
            long qty = 1 + rng() % 100;
            ops.push_back({OpType::Add, next_id, side, price, qty});
            live.push_back(next_id);
            next_id++;
        }
    }
    return ops;
}

void mix(uint64_t& sum, uint64_t x) { sum = sum * 1000003 + x; }

void mix_bbo(uint64_t& sum, const BBO& b) {
    mix(sum, b.has_bid);
    mix(sum, b.bid_price);
    mix(sum, b.bid_qty);
    mix(sum, b.has_ask);
    mix(sum, b.ask_price);
    mix(sum, b.ask_qty);
}

void mix_levels(uint64_t& sum, const std::vector<Level>& levels) {
    mix(sum, levels.size());
    for (const Level& l : levels) {
        mix(sum, l.price);
        mix(sum, l.qty);
    }
}

long long ns_between(Clock::time_point a, Clock::time_point b) {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(b - a).count();
}

struct RunResult {
    double total_ms = 0;
    double add_ns = 0;
    double cancel_ns = 0;
    double bbo_ns = 0;
    uint64_t checksum = 0;
    BBO final_bbo;
};

// Pass 1 has no per-op clock calls, so it gives the real total time.
// Pass 2 wraps every add / cancel / bbo call in its own steady_clock pair.
// Both passes feed the same checksum, so the compiler cannot drop the work.
template <class Book>
RunResult run_once(const std::vector<Op>& ops) {
    RunResult res;

    {
        Book book;
        uint64_t sum = 0;
        auto start = Clock::now();
        for (size_t i = 0; i < ops.size(); i++) {
            const Op& op = ops[i];
            if (op.type == OpType::Add) book.add_order(op.id, op.side, op.price, op.qty);
            else if (op.type == OpType::Cancel) mix(sum, book.cancel_order(op.id));
            else mix_bbo(sum, book.get_bbo());
            if ((i + 1) % 100 == 0) {
                mix_levels(sum, book.get_bids(10));
                mix_levels(sum, book.get_asks(10));
            }
        }
        auto end = Clock::now();
        res.total_ms = ns_between(start, end) / 1e6;
        res.final_bbo = book.get_bbo();
        mix_bbo(sum, res.final_bbo);
        res.checksum = sum;
    }

    {
        Book book;
        uint64_t sum = 0;
        long long add_time = 0, cancel_time = 0, bbo_time = 0;
        long long adds = 0, cancels = 0, bbos = 0;
        for (size_t i = 0; i < ops.size(); i++) {
            const Op& op = ops[i];
            if (op.type == OpType::Add) {
                auto t0 = Clock::now();
                book.add_order(op.id, op.side, op.price, op.qty);
                auto t1 = Clock::now();
                add_time += ns_between(t0, t1);
                adds++;
            } else if (op.type == OpType::Cancel) {
                auto t0 = Clock::now();
                bool ok = book.cancel_order(op.id);
                auto t1 = Clock::now();
                cancel_time += ns_between(t0, t1);
                cancels++;
                mix(sum, ok);
            } else {
                auto t0 = Clock::now();
                BBO b = book.get_bbo();
                auto t1 = Clock::now();
                bbo_time += ns_between(t0, t1);
                bbos++;
                mix_bbo(sum, b);
            }
            if ((i + 1) % 100 == 0) {
                mix_levels(sum, book.get_bids(10));
                mix_levels(sum, book.get_asks(10));
            }
        }
        mix_bbo(sum, book.get_bbo());
        res.add_ns = adds ? (double)add_time / adds : 0;
        res.cancel_ns = cancels ? (double)cancel_time / cancels : 0;
        res.bbo_ns = bbos ? (double)bbo_time / bbos : 0;
        if (sum != res.checksum) {
            std::cerr << "internal error: timed pass checksum differs from untimed pass\n";
            std::exit(1);
        }
    }
    return res;
}

double median(std::vector<double> v) {
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

// Cost of one empty steady_clock pair, so the per-op numbers can be read fairly.
double clock_overhead_ns() {
    const int n = 1000000;
    long long total = 0;
    for (int i = 0; i < n; i++) {
        auto t0 = Clock::now();
        auto t1 = Clock::now();
        total += ns_between(t0, t1);
    }
    return (double)total / n;
}

template <class Book>
void bench(const std::string& name, const std::vector<Op>& ops, int runs) {
    std::cout << "Implementation " << name << "\n";
    std::cout << "ops: " << ops.size() << ", runs: " << runs << "\n\n";
    std::printf("%-5s %12s %12s %14s %12s\n", "run", "total (ms)", "ns/add", "ns/cancel", "ns/bbo");

    std::vector<double> total, add, cancel, bbo;
    RunResult last;
    for (int r = 0; r < runs; r++) {
        last = run_once<Book>(ops);
        total.push_back(last.total_ms);
        add.push_back(last.add_ns);
        cancel.push_back(last.cancel_ns);
        bbo.push_back(last.bbo_ns);
        std::printf("%-5d %12.2f %12.1f %14.1f %12.1f\n", r + 1, last.total_ms, last.add_ns,
                    last.cancel_ns, last.bbo_ns);
    }
    std::printf("%-5s %12.2f %12.1f %14.1f %12.1f\n", "med", median(total), median(add),
                median(cancel), median(bbo));

    std::printf("\n(one empty steady_clock pair costs about %.1f ns; it is included in each per-op number)\n",
                clock_overhead_ns());

    const BBO& b = last.final_bbo;
    std::cout << "\nFinal BBO:\n";
    if (b.has_bid) std::cout << "Bid: " << price_str(b.bid_price) << " x " << b.bid_qty << "\n";
    else std::cout << "Bid: none\n";
    if (b.has_ask) std::cout << "Ask: " << price_str(b.ask_price) << " x " << b.ask_qty << "\n";
    else std::cout << "Ask: none\n";
    std::cout << "\nChecksum: " << last.checksum << "\n";
}

// ---------------------------------------------------------------- --verify

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::cout << "  FAILED: " << what << "\n";
        failures++;
    }
}

// Hand-written cases that every book must pass on its own.
template <class Book>
bool unit_tests(const char* name) {
    int before = failures;
    std::cout << "Unit tests " << name << ": ";
    {
        Book b;  // empty book
        BBO x = b.get_bbo();
        check(!x.has_bid && !x.has_ask, "empty book has no bid and no ask");
        check(b.get_bids(10).empty() && b.get_asks(10).empty(), "empty book has no levels");
        check(!b.cancel_order(12345), "cancel unknown id returns false");
    }
    {
        Book b;
        b.add_order(1, Side::Buy, 9998, 10);
        b.add_order(2, Side::Buy, 9998, 5);
        b.add_order(3, Side::Buy, 9990, 7);
        b.add_order(4, Side::Sell, 10001, 3);
        BBO x = b.get_bbo();
        check(x.has_bid && x.bid_price == 9998 && x.bid_qty == 15, "bid qty is the sum at a price");
        check(x.has_ask && x.ask_price == 10001 && x.ask_qty == 3, "best ask");
        check(b.get_bids(10).size() == 2, "depth 10 with 2 bid levels returns 2");
        check(b.get_asks(10).size() == 1, "depth 10 with 1 ask level returns 1");
        check(b.get_bids(1).size() == 1, "depth 1 returns 1");

        check(b.cancel_order(1), "cancel live order");
        check(!b.cancel_order(1), "cancel same id twice returns false");
        check(b.get_bbo().bid_qty == 5, "qty drops after cancel");
        check(b.cancel_order(2), "cancel last order at best level");
        check(b.get_bbo().bid_price == 9990 && b.get_bbo().bid_qty == 7, "best bid moves down");
        check(b.cancel_order(3) && !b.get_bbo().has_bid, "bid side empties");
        b.add_order(5, Side::Buy, 9998, 1);
        check(b.get_bbo().bid_price == 9998 && b.get_bbo().bid_qty == 1, "level refills after being empty");
    }
    {
        Book b;  // band edges and rejected orders
        b.add_order(1, Side::Buy, 5000, 1);
        b.add_order(2, Side::Buy, 9999, 1);
        b.add_order(3, Side::Sell, 10001, 1);
        b.add_order(4, Side::Sell, 15000, 1);
        check(b.get_bids(10).size() == 2 && b.get_asks(10).size() == 2, "band edges accepted");
        b.add_order(5, Side::Buy, 10000, 1);
        b.add_order(6, Side::Sell, 10000, 1);
        b.add_order(7, Side::Buy, 4999, 1);
        b.add_order(8, Side::Sell, 15001, 1);
        b.add_order(9, Side::Buy, 9000, 0);
        b.add_order(1, Side::Buy, 9000, 5);  // duplicate live id
        check(b.get_bids(10).size() == 2 && b.get_asks(10).size() == 2, "out of band / bad qty / duplicate rejected");
        check(!b.cancel_order(5) && !b.cancel_order(9), "rejected orders cannot be cancelled");
        check(b.get_bids(10)[1].price == 5000 && b.get_bids(10)[1].qty == 1, "duplicate id did not change qty");
        check(b.get_bids(10)[0].price == 9999 && b.get_asks(10)[1].price == 15000, "levels are best first");
    }
    {
        Book b;  // ids far apart and large
        b.add_order(1ULL << 40, Side::Buy, 7000, 4);
        b.add_order(~0ULL, Side::Sell, 12000, 6);
        check(b.cancel_order(1ULL << 40) && b.cancel_order(~0ULL), "large ids work");
    }
    bool ok = failures == before;
    std::cout << (ok ? "PASS" : "FAIL") << "\n";
    return ok;
}

// Runs the same workload through A, B and C and compares them after every
// operation. A is the reference, B and C must match it.
bool compare_workload(const char* label, const std::vector<Op>& ops, bool extra_bad_cancels,
                      bool ok[3]) {
    MapBook a;
    HeapBook b;
    ArrayBook c;
    std::mt19937 rng(7);
    size_t cancelled_count = 0;
    std::vector<uint64_t> cancelled;
    size_t compared = 0;

    auto same = [&](const char* what, size_t i, bool okb, bool okc) {
        if (!okb && ok[1]) { std::cout << "  " << label << ": B differs at op " << i << " (" << what << ")\n"; ok[1] = false; }
        if (!okc && ok[2]) { std::cout << "  " << label << ": C differs at op " << i << " (" << what << ")\n"; ok[2] = false; }
    };

    for (size_t i = 0; i < ops.size(); i++) {
        const Op& op = ops[i];
        if (op.type == OpType::Add) {
            a.add_order(op.id, op.side, op.price, op.qty);
            b.add_order(op.id, op.side, op.price, op.qty);
            c.add_order(op.id, op.side, op.price, op.qty);
        } else if (op.type == OpType::Cancel) {
            bool ra = a.cancel_order(op.id), rb = b.cancel_order(op.id), rc = c.cancel_order(op.id);
            same("cancel result", i, ra == rb, ra == rc);
            if (extra_bad_cancels) {
                cancelled.push_back(op.id);
                cancelled_count++;
            }
        } else {
            BBO ba = a.get_bbo();
            same("bbo", i, same_bbo(ba, b.get_bbo()), same_bbo(ba, c.get_bbo()));
        }
        if (extra_bad_cancels && i % 5 == 0) {
            // already-cancelled id or an id that never existed: all must say false
            uint64_t id = (cancelled_count && rng() % 2) ? cancelled[rng() % cancelled_count]
                                                         : 1000000000ULL + rng() % 1000;
            bool ra = a.cancel_order(id), rb = b.cancel_order(id), rc = c.cancel_order(id);
            same("cancel of dead/unknown id", i, ra == false && rb == false, rc == false);
        }
        if (i % 100 == 99) {
            for (int depth : {1, 10, 40}) {
                auto bids = a.get_bids(depth), asks = a.get_asks(depth);
                same("get_bids", i, same_levels(bids, b.get_bids(depth)), same_levels(bids, c.get_bids(depth)));
                same("get_asks", i, same_levels(asks, b.get_asks(depth)), same_levels(asks, c.get_asks(depth)));
            }
        }
        compared++;
    }
    // final full dump of both sides
    auto bids = a.get_bids(LEVELS_PER_SIDE), asks = a.get_asks(LEVELS_PER_SIDE);
    same("final bids", ops.size(), same_levels(bids, b.get_bids(LEVELS_PER_SIDE)), same_levels(bids, c.get_bids(LEVELS_PER_SIDE)));
    same("final asks", ops.size(), same_levels(asks, b.get_asks(LEVELS_PER_SIDE)), same_levels(asks, c.get_asks(LEVELS_PER_SIDE)));
    std::cout << "  " << label << ": " << compared << " operations compared, final book has "
              << bids.size() << " bid levels and " << asks.size() << " ask levels\n";
    return ok[1] && ok[2];
}

int run_verify(int ops) {
    std::cout << "Running correctness test...\n\n";
    bool pass[3];
    pass[0] = unit_tests<MapBook>("A");
    bool ub = unit_tests<HeapBook>("B");
    bool uc = unit_tests<ArrayBook>("C");

    std::cout << "\nRandom workloads (A is the reference):\n";
    bool ok[3] = {pass[0], ub, uc};
    compare_workload("benchmark workload (full price band)", make_workload(ops, 42), false, ok);
    compare_workload("narrow prices (levels empty and refill)", make_workload(ops / 4, 1, 6, 45), true, ok);
    compare_workload("narrow prices, mostly cancels", make_workload(ops / 4, 2, 3, 60), true, ok);

    std::cout << "\n";
    const char* names[3] = {"A", "B", "C"};
    for (int i = 0; i < 3; i++)
        std::cout << "Implementation " << names[i] << ": " << (ok[i] ? "PASS" : "FAIL") << "\n";
    bool all = ok[0] && ok[1] && ok[2];
    std::cout << "\n" << (all ? "All implementations produce identical results." : "Implementations DISAGREE.") << "\n";
    return all ? 0 : 1;
}

int main(int argc, char** argv) {
    std::string impl;
    bool verify = false;
    int runs = 5, ops = 1000000;

    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--verify") verify = true;
        else if (a == "--impl" && i + 1 < argc) impl = argv[++i];
        else if (a == "--runs" && i + 1 < argc) runs = std::atoi(argv[++i]);
        else if (a == "--ops" && i + 1 < argc) ops = std::atoi(argv[++i]);
        else {
            std::cerr << "usage: " << argv[0] << " --verify | --impl A|B|C [--runs N] [--ops N]\n";
            return 2;
        }
    }

    if (verify) return run_verify(ops);

    if (runs < 1 || ops < 1 || (impl != "A" && impl != "B" && impl != "C")) {
        std::cerr << "usage: " << argv[0] << " --verify | --impl A|B|C [--runs N] [--ops N]\n";
        return 2;
    }
    std::vector<Op> workload = make_workload(ops, 42);
    if (impl == "A") bench<MapBook>("A (std::map)", workload, runs);
    else if (impl == "B") bench<HeapBook>("B (priority_queue + lazy deletion)", workload, runs);
    else bench<ArrayBook>("C (price-indexed array + bitmap)", workload, runs);
    return 0;
}

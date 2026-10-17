// Benchmark + correctness harness for the three order books.
//   ./bench --verify
//   ./bench --impl A|B|C [--ops N] [--seed S] [--runs R] [--no-breakdown]
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>

#include "../include/book_array.hpp"
#include "../include/book_map.hpp"
#include "../include/book_vector.hpp"

using clk = std::chrono::steady_clock;

// ---------------------------------------------------------------- workload
enum OpType : uint8_t { ADD, CANCEL, BBOQ };
struct Op {
    uint64_t id;
    int tick;
    int qty;
    OpType type;
    Side side;
};

// Pre-generated so every implementation sees the *identical* operation stream.
// Cancels always pick a live order (unless inject_invalid, used by --verify to
// also exercise cancel-of-unknown / already-cancelled ids).
static std::vector<Op> generate(size_t n, uint32_t seed, bool inject_invalid) {
    std::mt19937 rng(seed);
    std::vector<Op> ops;
    ops.reserve(n);
    std::vector<uint64_t> live, dead;
    uint64_t next_id = 1;
    auto make_add = [&]() {
        Side s = (rng() & 1) ? Side::Buy : Side::Sell;
        int tick = (s == Side::Buy) ? kMinBidTick + static_cast<int>(rng() % 5000)
                                    : kMinAskTick + static_cast<int>(rng() % 5000);
        Op o{next_id++, tick, 1 + static_cast<int>(rng() % 100), ADD, s};
        live.push_back(o.id);
        return o;
    };
    for (size_t i = 0; i < n; ++i) {
        if (inject_invalid && rng() % 100 < 4) {  // cancel of unknown / already-cancelled id
            uint64_t id = (!dead.empty() && (rng() & 1)) ? dead[rng() % dead.size()]
                                                         : (uint64_t(1) << 40) + rng();
            ops.push_back({id, 0, 0, CANCEL, Side::Buy});
            continue;
        }
        unsigned r = rng() % 100;
        if (r < 70 || (r < 90 && live.empty())) {
            ops.push_back(make_add());
        } else if (r < 90) {
            size_t k = rng() % live.size();
            uint64_t id = live[k];
            live[k] = live.back();
            live.pop_back();
            if (inject_invalid) dead.push_back(id);
            ops.push_back({id, 0, 0, CANCEL, Side::Buy});
        } else {
            ops.push_back({0, 0, 0, BBOQ, Side::Buy});
        }
    }
    return ops;
}

// ---------------------------------------------------------------- running
struct Stats {
    uint64_t checksum = 0;
    uint64_t add_ns = 0, cancel_ns = 0, bbo_ns = 0;
    uint64_t add_n = 0, cancel_n = 0, bbo_n = 0;
};

static inline void mix(uint64_t& h, uint64_t v) { h ^= v + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2); }
static inline void mix(uint64_t& h, const BBO& b) {
    mix(h, b.has_bid); mix(h, uint64_t(b.bid_price)); mix(h, uint64_t(b.bid_qty));
    mix(h, b.has_ask); mix(h, uint64_t(b.ask_price)); mix(h, uint64_t(b.ask_qty));
}
static inline void mix(uint64_t& h, const std::vector<Level>& v) {
    for (const Level& l : v) { mix(h, uint64_t(l.price)); mix(h, uint64_t(l.qty)); }
}

// Timed=true wraps every call in steady_clock::now() pairs (per-op-type breakdown).
// Timed=false is the clean loop used for total time and for perf.
template <class Book, bool Timed>
static void process(Book& book, const std::vector<Op>& ops, Stats& st) {
    uint64_t h = 0;
    for (size_t i = 0; i < ops.size(); ++i) {
        const Op& op = ops[i];
        clk::time_point t0;
        if constexpr (Timed) t0 = clk::now();
        switch (op.type) {
            case ADD: {
                bool ok = book.add_order(op.id, op.side, op.tick, op.qty);
                if constexpr (Timed) { st.add_ns += (clk::now() - t0).count(); ++st.add_n; }
                mix(h, ok);
                break;
            }
            case CANCEL: {
                bool ok = book.cancel_order(op.id);
                if constexpr (Timed) { st.cancel_ns += (clk::now() - t0).count(); ++st.cancel_n; }
                mix(h, ok);
                break;
            }
            case BBOQ: {
                BBO b = book.get_bbo();
                if constexpr (Timed) { st.bbo_ns += (clk::now() - t0).count(); ++st.bbo_n; }
                mix(h, b);
                break;
            }
        }
        if ((i + 1) % 100 == 0) {  // every 100 ops: depth-10 snapshots (not in per-op timings)
            mix(h, book.get_bids(10));
            mix(h, book.get_asks(10));
        }
    }
    st.checksum = h;
}

static double timer_overhead_ns() {  // cost of one clk::now() pair, subtracted from per-op times
    constexpr int N = 2000000;
    uint64_t acc = 0;
    for (int i = 0; i < N; ++i) {
        auto a = clk::now();
        auto b = clk::now();
        acc += (b - a).count();
    }
    volatile uint64_t sink = acc;
    (void)sink;
    return double(acc) / N;
}

static double median(std::vector<double> v) {
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

struct Args {
    char impl = 0;
    bool verify = false, breakdown = true;
    size_t ops = 1000000, verify_ops = 200000;
    uint32_t seed = 42;
    int runs = 5;
};

template <class Book>
static int run_bench(const Args& a) {
    std::vector<Op> ops = generate(a.ops, a.seed, false);
    double overhead = a.breakdown ? timer_overhead_ns() : 0.0;
    std::vector<double> total, add, canc, bbo;
    uint64_t checksum = 0;
    BBO final_bbo{};
    for (int r = 0; r < a.runs; ++r) {
        {
            Book book;
            Stats st;
            auto t0 = clk::now();
            process<Book, false>(book, ops, st);
            auto t1 = clk::now();
            total.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
            checksum = st.checksum;
            final_bbo = book.get_bbo();
        }
        if (a.breakdown) {
            Book book;
            Stats st;
            process<Book, true>(book, ops, st);
            auto per = [&](uint64_t ns, uint64_t n) { return std::max(0.0, double(ns) / n - overhead); };
            add.push_back(per(st.add_ns, st.add_n));
            canc.push_back(per(st.cancel_ns, st.cancel_n));
            bbo.push_back(per(st.bbo_ns, st.bbo_n));
        }
    }
    std::printf("Implementation %s\n", Book::name());
    std::printf("Operations: %zu   Seed: %u   Runs: %d (median reported)\n", a.ops, a.seed, a.runs);
    std::printf("Total time:         %.2f ms\n", median(total));
    if (a.breakdown) {
        std::printf("ns / add_order:     %.1f\n", median(add));
        std::printf("ns / cancel_order:  %.1f\n", median(canc));
        std::printf("ns / get_bbo:       %.1f\n", median(bbo));
        std::printf("(timer overhead subtracted: %.1f ns per clock::now() pair)\n", overhead);
    }
    std::printf("Final BBO:\n");
    if (final_bbo.has_bid) std::printf("  Bid: %s x %ld\n", fmt_price(final_bbo.bid_price).c_str(), final_bbo.bid_qty);
    else std::printf("  Bid: none\n");
    if (final_bbo.has_ask) std::printf("  Ask: %s x %ld\n", fmt_price(final_bbo.ask_price).c_str(), final_bbo.ask_qty);
    else std::printf("  Ask: none\n");
    std::printf("Checksum: %llu\n", static_cast<unsigned long long>(checksum));
    return 0;
}

// ---------------------------------------------------------------- correctness
#define EXPECT(c) do { if (!(c)) { why = #c; return false; } } while (0)

template <class Book>
static bool edge_tests(std::string& why) {
    Book b;
    EXPECT(!b.get_bbo().has_bid && !b.get_bbo().has_ask);          // empty book
    EXPECT(b.get_bids(10).empty() && b.get_asks(10).empty());
    EXPECT(!b.cancel_order(12345));                                // unknown id
    EXPECT(b.add_order(1, Side::Buy, 9998, 10));
    EXPECT(b.add_order(2, Side::Buy, 9998, 5));                    // same price, 2 orders
    EXPECT(b.add_order(3, Side::Buy, 9997, 7));
    EXPECT(b.get_bbo().bid_price == 9998 && b.get_bbo().bid_qty == 15);
    EXPECT(b.get_bids(10).size() == 2);                            // only 2 levels exist
    EXPECT(b.add_order(4, Side::Buy, 9990, 1));
    EXPECT(b.get_bids(10).size() == 3);                            // insufficient depth
    EXPECT(b.get_bids(2).size() == 2);
    EXPECT(b.cancel_order(1));
    EXPECT(!b.cancel_order(1));                                    // already cancelled
    EXPECT(b.get_bbo().bid_qty == 5);
    EXPECT(b.cancel_order(2));
    EXPECT(b.get_bbo().bid_price == 9997 && b.get_bbo().bid_qty == 7);
    EXPECT(b.add_order(10, Side::Sell, 10001, 3));
    EXPECT(b.add_order(11, Side::Sell, 10001, 4));
    EXPECT(b.add_order(12, Side::Sell, 15000, 9));
    EXPECT(b.get_bbo().ask_price == 10001 && b.get_bbo().ask_qty == 7);
    EXPECT(b.cancel_order(10) && b.cancel_order(11));
    EXPECT(b.get_bbo().ask_price == 15000 && b.get_bbo().ask_qty == 9);
    EXPECT(!b.add_order(99, Side::Buy, 10001, 1));                 // bid outside bid band
    EXPECT(!b.add_order(98, Side::Sell, 9999, 1));                 // ask outside ask band
    EXPECT(!b.add_order(3, Side::Buy, 9000, 1));                   // duplicate id
    EXPECT(b.add_order(5, Side::Buy, 5000, 1));                    // band edge
    EXPECT(b.cancel_order(12) && !b.get_bbo().has_ask);            // book side empties
    return true;
}

static int run_verify(const Args& a) {
    std::printf("Running correctness test...\n");
    std::string why;
    bool unit[3];
    unit[0] = edge_tests<OrderBookMap>(why);    if (!unit[0]) std::printf("  A edge-case failed: %s\n", why.c_str());
    unit[1] = edge_tests<OrderBookVector>(why); if (!unit[1]) std::printf("  B edge-case failed: %s\n", why.c_str());
    unit[2] = edge_tests<OrderBookArray>(why);  if (!unit[2]) std::printf("  C edge-case failed: %s\n", why.c_str());

    std::vector<Op> ops = generate(a.verify_ops, a.seed, true);
    OrderBookMap A;
    OrderBookVector B;
    OrderBookArray C;
    size_t bad_count[3] = {0, 0, 0};
    long first_bad[3] = {-1, -1, -1};
    size_t checks = 0;

    auto report = [&](bool e01, bool e02, bool e12, size_t i) {
        ++checks;
        if (e01 && e02 && e12) return;
        bool bad[3] = {false, false, false};
        if (e01) bad[2] = true;            // A==B, so C is the odd one out
        else if (e02) bad[1] = true;
        else if (e12) bad[0] = true;
        else bad[0] = bad[1] = bad[2] = true;
        for (int k = 0; k < 3; ++k)
            if (bad[k]) { if (!bad_count[k]++) first_bad[k] = static_cast<long>(i); }
    };

    for (size_t i = 0; i < ops.size(); ++i) {
        const Op& op = ops[i];
        if (op.type == ADD) {
            bool a0 = A.add_order(op.id, op.side, op.tick, op.qty);
            bool b0 = B.add_order(op.id, op.side, op.tick, op.qty);
            bool c0 = C.add_order(op.id, op.side, op.tick, op.qty);
            report(a0 == b0, a0 == c0, b0 == c0, i);
        } else if (op.type == CANCEL) {
            bool a0 = A.cancel_order(op.id), b0 = B.cancel_order(op.id), c0 = C.cancel_order(op.id);
            report(a0 == b0, a0 == c0, b0 == c0, i);
        }
        BBO ba = A.get_bbo(), bb = B.get_bbo(), bc = C.get_bbo();
        report(ba == bb, ba == bc, bb == bc, i);
        auto da = A.get_bids(10), db = B.get_bids(10), dc = C.get_bids(10);
        report(da == db, da == dc, db == dc, i);
        auto sa = A.get_asks(10), sb = B.get_asks(10), sc = C.get_asks(10);
        report(sa == sb, sa == sc, sb == sc, i);
        if (i % 1000 == 0) {  // full-depth comparison of the whole book
            auto fa = A.get_bids(100000), fb = B.get_bids(100000), fc = C.get_bids(100000);
            report(fa == fb, fa == fc, fb == fc, i);
            auto ga = A.get_asks(100000), gb = B.get_asks(100000), gc = C.get_asks(100000);
            report(ga == gb, ga == gc, gb == gc, i);
        }
    }

    const char* names = "ABC";
    bool all = true;
    for (int k = 0; k < 3; ++k) {
        bool pass = unit[k] && bad_count[k] == 0;
        all &= pass;
        std::printf("Implementation %c: %s", names[k], pass ? "PASS" : "FAIL");
        if (bad_count[k]) std::printf("  (%zu mismatches, first at op %ld)", bad_count[k], first_bad[k]);
        std::printf("\n");
    }
    std::printf("(%zu operations incl. invalid cancels, %zu comparisons: BBO, depth-10 bids/asks, full-book, cancel/add results)\n",
                ops.size(), checks);
    std::printf(all ? "\nAll implementations produce identical results.\n"
                    : "\nMISMATCH: implementations disagree.\n");
    return all ? 0 : 1;
}

int main(int argc, char** argv) {
    Args a;
    for (int i = 1; i < argc; ++i) {
        std::string s = argv[i];
        auto next = [&]() -> const char* { return (i + 1 < argc) ? argv[++i] : ""; };
        if (s == "--verify") a.verify = true;
        else if (s == "--impl") { const char* v = next(); a.impl = v[0] ? v[0] : 0; }
        else if (s == "--ops") a.ops = std::strtoull(next(), nullptr, 10);
        else if (s == "--seed") a.seed = static_cast<uint32_t>(std::strtoul(next(), nullptr, 10));
        else if (s == "--runs") a.runs = std::max(1, std::atoi(next()));
        else if (s == "--no-breakdown") a.breakdown = false;
        else { std::fprintf(stderr, "unknown argument: %s\n", s.c_str()); return 2; }
    }
    if (a.verify) return run_verify(a);
    switch (a.impl) {
        case 'A': case 'a': a.impl = 'A'; return run_bench<OrderBookMap>(a);
        case 'B': case 'b': a.impl = 'B'; return run_bench<OrderBookVector>(a);
        case 'C': case 'c': a.impl = 'C'; return run_bench<OrderBookArray>(a);
        default:
            std::fprintf(stderr, "usage: %s --verify | --impl A|B|C [--ops N] [--seed S] [--runs R] [--no-breakdown]\n", argv[0]);
            return 2;
    }
}

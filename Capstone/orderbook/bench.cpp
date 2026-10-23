// Benchmark + correctness check for the three order books.
//
// Usage:
//   ./bench --verify          run the same workload on A, B, C and compare
//   ./bench --impl A          benchmark implementation A (B or C too)
//   ./bench --impl A --ops 1000000 --seed 42
//
// The workload: 70% add, 20% cancel (always a live order), 10% BBO query,
// plus get_bids(10)/get_asks(10) every 100 ops. Fixed seed => repeatable.

#include "book_a.hpp"
#include "book_b.hpp"
#include "book_c.hpp"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>
#include <sys/resource.h>

// Total ops, fixed seed from the spec.
static const int DEFAULT_OPS = 1000000;
static const unsigned DEFAULT_SEED = 42;

// A cancel needs a live order id, picked in O(1): dense vector of live ids
// plus each id's position so removal is a swap-with-last.
struct LiveSet {
    std::vector<uint64_t> ids;
    std::unordered_map<uint64_t, size_t> pos;

    void add(uint64_t id) {
        pos[id] = ids.size();
        ids.push_back(id);
    }
    void remove(uint64_t id) {
        size_t i = pos[id];
        uint64_t last = ids.back();
        ids[i] = last;
        pos[last] = i;
        ids.pop_back();
        pos.erase(id);
    }
    bool empty() const { return ids.empty(); }
};

// One recorded op so verify + bench share the exact same workload.
struct Op {
    int kind; // 0 = add, 1 = cancel, 2 = bbo (+ depth query every 100th op)
    Side side;
    double price;
    long qty;
    uint64_t id; // add: new id, cancel: id to cancel
};

// Build the workload once. Cancels always target a live order; if the book
// is empty when a cancel comes up, turn it into an add instead.
std::vector<Op> build_workload(int n_ops, unsigned seed) {
    std::mt19937 rng(seed);
    std::uniform_real_distribution<double> frac(0.0, 1.0);
    std::uniform_int_distribution<int> qty(1, 100);
    std::uniform_int_distribution<int> bid_tick(5000, 9999);
    std::uniform_int_distribution<int> ask_tick(10001, 15000);

    std::vector<Op> ops;
    ops.reserve(n_ops);
    LiveSet live;
    uint64_t next_id = 1;

    for (int i = 0; i < n_ops; ++i) {
        double r = frac(rng);
        if (r < 0.70 || live.empty()) {
            // add
            Side s = (frac(rng) < 0.5) ? Side::Buy : Side::Sell;
            int tick = (s == Side::Buy) ? bid_tick(rng) : ask_tick(rng);
            Op o{0, s, tick_to_price(tick), (long)qty(rng), next_id++};
            ops.push_back(o);
            live.add(o.id);
        } else if (r < 0.90) {
            // cancel a random live order
            std::uniform_int_distribution<size_t> pick(0, live.ids.size() - 1);
            uint64_t id = live.ids[pick(rng)];
            ops.push_back({1, Side::Buy, 0, 0, id});
            live.remove(id);
        } else {
            // bbo query
            ops.push_back({2, Side::Buy, 0, 0, 0});
        }
    }
    return ops;
}

static bool bbo_equal(const BBO &a, const BBO &b) {
    return a.has_bid == b.has_bid && a.has_ask == b.has_ask &&
           (!a.has_bid || (a.bid_price == b.bid_price && a.bid_qty == b.bid_qty)) &&
           (!a.has_ask || (a.ask_price == b.ask_price && a.ask_qty == b.ask_qty));
}

static bool levels_equal(const std::vector<Level> &a, const std::vector<Level> &b) {
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (a[i].price != b[i].price || a[i].qty != b[i].qty)
            return false;
    return true;
}

// Run the workload on all three books, compare BBO + depth after every
// `check_every` ops and at the end. Also checks edge cases first.
int run_verify(const std::vector<Op> &ops) {
    printf("Running correctness test...\n");

    // Edge cases: empty book, unknown cancel, shallow depth. All three
    // books must behave the same here.
    {
        MapOrderBook a;
        HashScanOrderBook b;
        ArrayOrderBook c;
        bool ok = true;
        ok &= !a.get_bbo().has_bid && !b.get_bbo().has_bid && !c.get_bbo().has_bid;
        ok &= !a.get_bbo().has_ask && !b.get_bbo().has_ask && !c.get_bbo().has_ask;
        ok &= !a.cancel_order(999) && !b.cancel_order(999) && !c.cancel_order(999);
        a.add_order(1, Side::Buy, 99.50, 10);
        b.add_order(1, Side::Buy, 99.50, 10);
        c.add_order(1, Side::Buy, 99.50, 10);
        ok &= a.cancel_order(1) && b.cancel_order(1) && c.cancel_order(1);
        ok &= !a.cancel_order(1) && !b.cancel_order(1) && !c.cancel_order(1); // double cancel
        ok &= a.get_bids(10).size() == 0; // insufficient depth -> just what's there
        printf("Edge cases (empty BBO, unknown/double cancel, shallow depth): %s\n",
               ok ? "PASS" : "FAIL");
        if (!ok)
            return 1;
    }

    MapOrderBook a;
    HashScanOrderBook b;
    ArrayOrderBook c;
    bool pass_a = true, pass_b = true, pass_c = true;

    const int check_every = 1000;
    for (size_t i = 0; i < ops.size(); ++i) {
        const Op &o = ops[i];
        if (o.kind == 0) {
            a.add_order(o.id, o.side, o.price, o.qty);
            b.add_order(o.id, o.side, o.price, o.qty);
            c.add_order(o.id, o.side, o.price, o.qty);
        } else if (o.kind == 1) {
            bool ra = a.cancel_order(o.id);
            bool rb = b.cancel_order(o.id);
            bool rc = c.cancel_order(o.id);
            if (!(ra && rb && rc)) { // cancel of a live order must succeed everywhere
                pass_a &= ra;
                pass_b &= rb;
                pass_c &= rc;
            }
        } else {
            (void)a.get_bbo();
            (void)b.get_bbo();
            (void)c.get_bbo();
        }
        if (o.kind == 2 || (i + 1) % 100 == 0) {
            // depth queries also run every 100 ops, like the benchmark
            (void)a.get_bids(10);
            (void)b.get_bids(10);
            (void)c.get_bids(10);
        }
        if ((i + 1) % check_every == 0 || i + 1 == ops.size()) {
            // B takes the reference role here only because all three ran the
            // identical op stream; any of them could be the reference.
            BBO ref = a.get_bbo();
            pass_b &= bbo_equal(ref, b.get_bbo());
            pass_c &= bbo_equal(ref, c.get_bbo());
            pass_b &= levels_equal(a.get_bids(10), b.get_bids(10));
            pass_c &= levels_equal(a.get_bids(10), c.get_bids(10));
            pass_b &= levels_equal(a.get_asks(10), b.get_asks(10));
            pass_c &= levels_equal(a.get_asks(10), c.get_asks(10));
            if (!pass_b || !pass_c) {
                printf("MISMATCH at op %zu\n", i + 1);
                break;
            }
        }
    }

    printf("Implementation A: %s\n", pass_a ? "PASS" : "FAIL");
    printf("Implementation B: %s\n", pass_b ? "PASS" : "FAIL");
    printf("Implementation C: %s\n", pass_c ? "PASS" : "FAIL");
    if (pass_a && pass_b && pass_c)
        printf("All implementations produce identical results.\n");
    return (pass_a && pass_b && pass_c) ? 0 : 1;
}

// Benchmark one implementation. Timers are per op type. `sink` stops the
// compiler from throwing away the queries (plus the final printout).
template <typename Book>
void run_bench(const std::vector<Op> &ops, const char *name) {
    using clock = std::chrono::steady_clock;
    Book book;
    long n_add = 0, n_cancel = 0, n_bbo = 0, n_depth = 0;
    long long t_add = 0, t_cancel = 0, t_bbo = 0, t_depth = 0;
    volatile long sink = 0; // volatile so reads can't be optimized out

    auto t0 = clock::now();
    for (size_t i = 0; i < ops.size(); ++i) {
        const Op &o = ops[i];
        if (o.kind == 0) {
            auto s = clock::now();
            book.add_order(o.id, o.side, o.price, o.qty);
            t_add += (clock::now() - s).count();
            ++n_add;
        } else if (o.kind == 1) {
            auto s = clock::now();
            sink += book.cancel_order(o.id) ? 1 : 0;
            t_cancel += (clock::now() - s).count();
            ++n_cancel;
        } else {
            auto s = clock::now();
            BBO b = book.get_bbo();
            t_bbo += (clock::now() - s).count();
            ++n_bbo;
            sink += (long)b.bid_qty + (long)b.ask_qty;
        }
        if ((i + 1) % 100 == 0) {
            auto s = clock::now();
            auto bids = book.get_bids(10);
            auto asks = book.get_asks(10);
            t_depth += (clock::now() - s).count();
            ++n_depth;
            sink += (long)bids.size() + (long)asks.size();
        }
    }
    auto total = clock::now() - t0;

    // Checksum over the final state so the whole run is observable.
    BBO f = book.get_bbo();
    long checksum = sink + (long)(f.bid_price * 100) + f.bid_qty +
                    (long)(f.ask_price * 100) + f.ask_qty;

    double total_ms = (double)total.count() / 1e6;
    printf("impl=%s ops=%zu seed=42\n", name, ops.size());
    printf("total_ms=%.2f\n", total_ms);
    printf("add:    count=%ld ns/op=%.1f\n", n_add, (double)t_add / n_add);
    printf("cancel: count=%ld ns/op=%.1f\n", n_cancel, (double)t_cancel / n_cancel);
    printf("bbo:    count=%ld ns/op=%.1f\n", n_bbo, (double)t_bbo / n_bbo);
    printf("depth(10+10 per 100 ops): count=%ld ns/op=%.1f\n", n_depth,
           n_depth ? (double)t_depth / n_depth : 0);
    printf("Final BBO:\n");
    if (f.has_bid)
        printf("Bid: %.2f x %ld\n", f.bid_price, f.bid_qty);
    else
        printf("Bid: (none)\n");
    if (f.has_ask)
        printf("Ask: %.2f x %ld\n", f.ask_price, f.ask_qty);
    else
        printf("Ask: (none)\n");
    printf("Checksum: %ld\n", checksum);

    // Peak memory + page faults from the OS. getrusage is free to call
    // and works everywhere, unlike perf counters.
    struct rusage ru;
    getrusage(RUSAGE_SELF, &ru);
    printf("Peak RSS: %ld KB, minor faults: %ld, major faults: %ld\n",
           ru.ru_maxrss, ru.ru_minflt, ru.ru_majflt);
}

int main(int argc, char **argv) {
    std::string impl = "A";
    int n_ops = DEFAULT_OPS;
    unsigned seed = DEFAULT_SEED;
    bool verify = false;

    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--verify"))
            verify = true;
        else if (!strcmp(argv[i], "--impl") && i + 1 < argc)
            impl = argv[++i];
        else if (!strcmp(argv[i], "--ops") && i + 1 < argc)
            n_ops = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--seed") && i + 1 < argc)
            seed = (unsigned)atoi(argv[++i]);
    }

    std::vector<Op> ops = build_workload(n_ops, seed);

    if (verify)
        return run_verify(ops);

    if (impl == "A")
        run_bench<MapOrderBook>(ops, "A");
    else if (impl == "B")
        run_bench<HashScanOrderBook>(ops, "B");
    else if (impl == "C")
        run_bench<ArrayOrderBook>(ops, "C");
    else {
        printf("unknown impl '%s', use A, B or C\n", impl.c_str());
        return 1;
    }
    return 0;
}

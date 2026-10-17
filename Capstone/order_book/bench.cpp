// build:  g++ -O2 -std=c++20 -Wall -o bench bench.cpp
//
// ./bench --verify          check that A, B and C give the same answers
// ./bench --impl A          benchmark one book (A = map, B = heap, C = array)
// ./bench --impl C --runs 1 one run only (useful under perf stat -r 5)

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <random>
#include <string>
#include <vector>

#include "book_array.h"
#include "book_heap.h"
#include "book_map.h"

const int kNumOps = 1000000;
const uint32_t kSeed = 42;
const int kDepthEvery = 100;  // every 100 ops we also call get_bids(10) and get_asks(10)

// global so the compiler can not throw away the work (same trick as g_sink in lec 12)
uint64_t g_sink = 0;

// ---------------------------------------------------------------------------
// workload
// ---------------------------------------------------------------------------

enum OpType { kAdd, kCancel, kBbo };

struct Op {
    uint64_t id;
    int32_t tick;
    int32_t qty;
    OpType type;
    Side side;
};

// the whole workload is made once before timing starts
// so random number work is not counted inside the order book timings
// and every book gets exactly the same list of ops
std::vector<Op> make_workload(int n, uint32_t seed) {
    // std::mt19937 is a random number generator from <random>
    // the same seed always gives the same numbers on every machine, so the run is repeatable
    // I use rng() % n directly because the distributions in <random> can give
    // different numbers on different compilers
    std::mt19937 rng(seed);

    std::vector<Op> ops;
    ops.reserve(n);
    std::vector<uint64_t> live;  // ids that are resting right now, cancels pick from here
    live.reserve(n);
    uint64_t next_id = 1;

    for (int i = 0; i < n; i++) {
        uint32_t r = rng() % 100;
        Op op;

        // 70% add, 20% cancel, 10% bbo
        // if there is nothing live to cancel the cancel turns into an add
        if (r < 70 || (r < 90 && live.empty())) {
            op.type = kAdd;
            op.id = next_id++;
            op.side = (rng() % 2 == 0) ? Side::Buy : Side::Sell;
            if (op.side == Side::Buy) {
                op.tick = kMinBidTick + (int32_t)(rng() % (kMaxBidTick - kMinBidTick + 1));
            } else {
                op.tick = kMinAskTick + (int32_t)(rng() % (kMaxAskTick - kMinAskTick + 1));
            }
            op.qty = 1 + (int32_t)(rng() % 100);
            live.push_back(op.id);
        } else if (r < 90) {
            op.type = kCancel;
            size_t pick = rng() % live.size();
            op.id = live[pick];
            // swap with the last one and pop, so removing is O(1)
            live[pick] = live.back();
            live.pop_back();
            op.tick = 0;
            op.qty = 0;
            op.side = Side::Buy;
        } else {
            op.type = kBbo;
            op.id = 0;
            op.tick = 0;
            op.qty = 0;
            op.side = Side::Buy;
        }
        ops.push_back(op);
    }
    return ops;
}

// ---------------------------------------------------------------------------
// small helpers
// ---------------------------------------------------------------------------

// prints a tick as a normal price without using double, 9998 -> 99.98
void print_price(int32_t tick) {
    std::printf("%d.%02d", tick / 100, tick % 100);
}

void print_bbo(const BBO& b) {
    std::printf("Final BBO:\n");
    std::printf("Bid: ");
    if (b.has_bid) {
        print_price(b.bid_tick);
        std::printf(" x %ld\n", b.bid_qty);
    } else {
        std::printf("none\n");
    }
    std::printf("Ask: ");
    if (b.has_ask) {
        print_price(b.ask_tick);
        std::printf(" x %ld\n", b.ask_qty);
    } else {
        std::printf("none\n");
    }
}

uint64_t levels_sum(const std::vector<Level>& v) {
    uint64_t s = 0;
    for (size_t i = 0; i < v.size(); i++) {
        s += (uint64_t)v[i].tick * 31 + (uint64_t)v[i].qty;
    }
    return s;
}

bool same_levels(const std::vector<Level>& a, const std::vector<Level>& b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (size_t i = 0; i < a.size(); i++) {
        if (a[i].tick != b[i].tick || a[i].qty != b[i].qty) {
            return false;
        }
    }
    return true;
}

long total_qty(const std::vector<Level>& v) {
    long s = 0;
    for (size_t i = 0; i < v.size(); i++) {
        s += v[i].qty;
    }
    return s;
}

// std::sort is from <algorithm>. I sort a copy and take the middle value as the median
double median(std::vector<double> v) {
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

// value at a given percent of a sorted list, 0.99 gives p99
long long percentile(std::vector<long long>& sorted, double p) {
    if (sorted.empty()) {
        return 0;
    }
    size_t idx = (size_t)(p * (sorted.size() - 1));
    return sorted[idx];
}

// ---------------------------------------------------------------------------
// benchmark
// ---------------------------------------------------------------------------

// std::chrono::steady_clock is a clock that only moves forward
// sir explained it reads a cpu counter and does not ask the kernel, so it is cheap to call
typedef std::chrono::steady_clock Clock;

long long ns_between(Clock::time_point a, Clock::time_point b) {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(b - a).count();
}

struct RunResult {
    double total_ms;
    double ns_add;
    double ns_cancel;
    double ns_bbo;
    double ns_depth;
    // tail latency of this run, to talk about how predictable each book is
    long long p99_add;
    long long p99_cancel;
    long long p99_bbo;
    long long max_cancel;
    BBO final_bbo;
    uint64_t checksum;
};

// one template for all three books so they all run the exact same code (hint 5)
// no virtual functions, the compiler makes one copy of this per book type
template <typename Book>
RunResult run_once(const std::vector<Op>& ops) {
    // the array book is about 80 KB plus the pool, so I put every book on the heap
    Book* book = new Book();
    g_sink = 0;

    std::vector<long long> add_t, cancel_t, bbo_t;
    add_t.reserve(ops.size());
    cancel_t.reserve(ops.size());
    bbo_t.reserve(ops.size());
    long long depth_ns = 0;
    long long depth_calls = 0;

    Clock::time_point start = Clock::now();
    for (size_t i = 0; i < ops.size(); i++) {
        const Op& op = ops[i];
        if (op.type == kAdd) {
            Clock::time_point t0 = Clock::now();
            book->add_order(op.id, op.side, op.tick, op.qty);
            Clock::time_point t1 = Clock::now();
            add_t.push_back(ns_between(t0, t1));
        } else if (op.type == kCancel) {
            Clock::time_point t0 = Clock::now();
            bool ok = book->cancel_order(op.id);
            Clock::time_point t1 = Clock::now();
            cancel_t.push_back(ns_between(t0, t1));
            g_sink += ok;
        } else {
            Clock::time_point t0 = Clock::now();
            BBO b = book->get_bbo();
            Clock::time_point t1 = Clock::now();
            bbo_t.push_back(ns_between(t0, t1));
            g_sink += b.bid_tick + b.bid_qty + b.ask_tick + b.ask_qty;
        }

        if ((i + 1) % kDepthEvery == 0) {
            Clock::time_point t0 = Clock::now();
            std::vector<Level> bids = book->get_bids(10);
            std::vector<Level> asks = book->get_asks(10);
            Clock::time_point t1 = Clock::now();
            depth_ns += ns_between(t0, t1);
            depth_calls++;
            g_sink += levels_sum(bids) + levels_sum(asks);
        }
    }
    Clock::time_point end = Clock::now();

    RunResult r;
    r.total_ms = ns_between(start, end) / 1e6;

    long long sum = 0;
    for (size_t i = 0; i < add_t.size(); i++) sum += add_t[i];
    r.ns_add = add_t.empty() ? 0 : (double)sum / add_t.size();
    sum = 0;
    for (size_t i = 0; i < cancel_t.size(); i++) sum += cancel_t[i];
    r.ns_cancel = cancel_t.empty() ? 0 : (double)sum / cancel_t.size();
    sum = 0;
    for (size_t i = 0; i < bbo_t.size(); i++) sum += bbo_t[i];
    r.ns_bbo = bbo_t.empty() ? 0 : (double)sum / bbo_t.size();
    r.ns_depth = depth_calls == 0 ? 0 : (double)depth_ns / depth_calls;

    std::sort(add_t.begin(), add_t.end());
    std::sort(cancel_t.begin(), cancel_t.end());
    std::sort(bbo_t.begin(), bbo_t.end());
    r.p99_add = percentile(add_t, 0.99);
    r.p99_cancel = percentile(cancel_t, 0.99);
    r.p99_bbo = percentile(bbo_t, 0.99);
    r.max_cancel = cancel_t.empty() ? 0 : cancel_t.back();

    r.final_bbo = book->get_bbo();
    r.checksum = g_sink;

    delete book;
    return r;
}

// how long two now() calls take back to back
// every timed op includes this, so it is good to know how big it is
double timer_overhead_ns() {
    const int n = 1000000;
    long long sum = 0;
    for (int i = 0; i < n; i++) {
        Clock::time_point t0 = Clock::now();
        Clock::time_point t1 = Clock::now();
        sum += ns_between(t0, t1);
    }
    return (double)sum / n;
}

template <typename Book>
void run_bench(const char* name, const std::vector<Op>& ops, int runs) {
    std::printf("Implementation %s\n", name);
    std::printf("ops: %d, seed: %u, runs: %d\n\n", (int)ops.size(), kSeed, runs);

    std::vector<double> total, add, cancel, bbo, depth;
    std::vector<double> p99_add, p99_cancel, p99_bbo, max_cancel;
    RunResult last;
    for (int i = 0; i < runs; i++) {
        last = run_once<Book>(ops);
        std::printf("run %d: total %.2f ms | add %.1f ns | cancel %.1f ns | bbo %.1f ns\n",
                    i + 1, last.total_ms, last.ns_add, last.ns_cancel, last.ns_bbo);
        total.push_back(last.total_ms);
        add.push_back(last.ns_add);
        cancel.push_back(last.ns_cancel);
        bbo.push_back(last.ns_bbo);
        depth.push_back(last.ns_depth);
        p99_add.push_back((double)last.p99_add);
        p99_cancel.push_back((double)last.p99_cancel);
        p99_bbo.push_back((double)last.p99_bbo);
        max_cancel.push_back((double)last.max_cancel);
    }

    std::printf("\nmedian of %d runs\n", runs);
    std::printf("total time (ms)      : %.2f\n", median(total));
    std::printf("ns / add             : %.1f\n", median(add));
    std::printf("ns / cancel          : %.1f\n", median(cancel));
    std::printf("ns / bbo             : %.1f\n", median(bbo));
    std::printf("ns / depth query     : %.1f   (get_bids(10) + get_asks(10))\n", median(depth));
    std::printf("p99 add / cancel / bbo (ns) : %.0f / %.0f / %.0f\n",
                median(p99_add), median(p99_cancel), median(p99_bbo));
    std::printf("max cancel (ns)      : %.0f\n", median(max_cancel));
    std::printf("timer overhead       : %.1f ns per now() pair (included in the numbers above)\n\n",
                timer_overhead_ns());

    print_bbo(last.final_bbo);
    std::printf("\nChecksum: %llu\n", (unsigned long long)last.checksum);
}

// ---------------------------------------------------------------------------
// correctness test
// ---------------------------------------------------------------------------

// small hand made cases from the "required behavior" part of the doc
template <typename Book>
bool edge_tests(const char* name) {
    Book* b = new Book();
    bool ok = true;

    BBO e = b->get_bbo();
    if (e.has_bid || e.has_ask) {
        std::printf("  %s: empty book should have no bid and no ask\n", name);
        ok = false;
    }
    if (b->cancel_order(12345)) {
        std::printf("  %s: cancel of unknown id should return false\n", name);
        ok = false;
    }

    b->add_order(1, Side::Buy, 9998, 10);
    b->add_order(2, Side::Buy, 9998, 5);   // same price, qty should add up
    b->add_order(3, Side::Buy, 9990, 7);
    b->add_order(4, Side::Buy, 9500, 1);
    b->add_order(5, Side::Sell, 10001, 20);
    b->add_order(6, Side::Buy, 10001, 3);  // buy in the ask band is not valid, ignored
    b->add_order(7, Side::Sell, 9000, 3);  // sell in the bid band is not valid, ignored

    BBO x = b->get_bbo();
    if (!x.has_bid || x.bid_tick != 9998 || x.bid_qty != 15 ||
        !x.has_ask || x.ask_tick != 10001 || x.ask_qty != 20) {
        std::printf("  %s: wrong bbo after adds\n", name);
        ok = false;
    }

    // only 3 bid levels exist, asking for 10 should give 3 in order high to low
    std::vector<Level> bids = b->get_bids(10);
    if (bids.size() != 3 || bids[0].tick != 9998 || bids[1].tick != 9990 || bids[2].tick != 9500) {
        std::printf("  %s: get_bids(10) should return the 3 levels\n", name);
        ok = false;
    }
    if (b->get_asks(10).size() != 1) {
        std::printf("  %s: get_asks(10) should return 1 level\n", name);
        ok = false;
    }

    if (!b->cancel_order(1)) {
        std::printf("  %s: cancel of live order should return true\n", name);
        ok = false;
    }
    if (b->cancel_order(1)) {
        std::printf("  %s: second cancel of same id should return false\n", name);
        ok = false;
    }
    if (b->get_bbo().bid_qty != 5) {
        std::printf("  %s: qty at best bid should drop to 5\n", name);
        ok = false;
    }

    // removing the last order at the best price should move the best bid down
    b->cancel_order(2);
    if (b->get_bbo().bid_tick != 9990) {
        std::printf("  %s: best bid should move to 99.90\n", name);
        ok = false;
    }

    // a price level that goes empty and then comes back
    b->add_order(8, Side::Buy, 9998, 4);
    if (b->get_bbo().bid_tick != 9998 || b->get_bbo().bid_qty != 4) {
        std::printf("  %s: level should come back after new add\n", name);
        ok = false;
    }

    b->cancel_order(3);
    b->cancel_order(4);
    b->cancel_order(5);
    b->cancel_order(8);
    BBO z = b->get_bbo();
    if (z.has_bid || z.has_ask) {
        std::printf("  %s: book should be empty again\n", name);
        ok = false;
    }

    delete b;
    return ok;
}

int run_verify() {
    std::printf("Running correctness test...\n\n");
    std::printf("sizeof(Order) = %zu bytes\n\n", sizeof(Order));

    bool edge_a = edge_tests<MapOrderBook>("A");
    bool edge_b = edge_tests<HeapOrderBook>("B");
    bool edge_c = edge_tests<ArrayOrderBook>("C");
    std::printf("edge case tests: A %s, B %s, C %s\n\n",
                edge_a ? "ok" : "FAIL", edge_b ? "ok" : "FAIL", edge_c ? "ok" : "FAIL");

    std::vector<Op> ops = make_workload(kNumOps, kSeed);
    std::printf("random workload: %d ops, seed %u\n", kNumOps, kSeed);
    std::printf("checking bbo after every op, top 10 levels every %d ops, full book every 10000 ops\n\n",
                kDepthEvery);

    MapOrderBook* a = new MapOrderBook();
    HeapOrderBook* b = new HeapOrderBook();
    ArrayOrderBook* c = new ArrayOrderBook();

    // B and C are compared with A at every step
    // A itself is checked by the edge tests and by the total qty check below
    long mismatch_b = 0;
    long mismatch_c = 0;
    long qty_bad_a = 0, qty_bad_b = 0, qty_bad_c = 0;
    long checks = 0;

    // expected total qty in the book, worked out only from the ops list
    std::vector<int32_t> qty_of(ops.size() + 2, 0);
    long expected_total = 0;

    for (size_t i = 0; i < ops.size(); i++) {
        const Op& op = ops[i];
        if (op.type == kAdd) {
            a->add_order(op.id, op.side, op.tick, op.qty);
            b->add_order(op.id, op.side, op.tick, op.qty);
            c->add_order(op.id, op.side, op.tick, op.qty);
            qty_of[op.id] = op.qty;
            expected_total += op.qty;
        } else if (op.type == kCancel) {
            bool ra = a->cancel_order(op.id);
            bool rb = b->cancel_order(op.id);
            bool rc = c->cancel_order(op.id);
            if (rb != ra) mismatch_b++;
            if (rc != ra) mismatch_c++;
            expected_total -= qty_of[op.id];
        }

        // bbo after every single op
        BBO ba = a->get_bbo();
        if (!same_bbo(ba, b->get_bbo())) mismatch_b++;
        if (!same_bbo(ba, c->get_bbo())) mismatch_c++;
        checks++;

        if ((i + 1) % kDepthEvery == 0) {
            std::vector<Level> bids_a = a->get_bids(10);
            std::vector<Level> asks_a = a->get_asks(10);
            if (!same_levels(bids_a, b->get_bids(10)) || !same_levels(asks_a, b->get_asks(10))) mismatch_b++;
            if (!same_levels(bids_a, c->get_bids(10)) || !same_levels(asks_a, c->get_asks(10))) mismatch_c++;
        }

        if ((i + 1) % 10000 == 0 || i + 1 == ops.size()) {
            // full depth of the book, every level and every qty
            std::vector<Level> fb_a = a->get_bids(kNumSlots);
            std::vector<Level> fa_a = a->get_asks(kNumSlots);
            std::vector<Level> fb_b = b->get_bids(kNumSlots);
            std::vector<Level> fa_b = b->get_asks(kNumSlots);
            std::vector<Level> fb_c = c->get_bids(kNumSlots);
            std::vector<Level> fa_c = c->get_asks(kNumSlots);
            if (!same_levels(fb_a, fb_b) || !same_levels(fa_a, fa_b)) mismatch_b++;
            if (!same_levels(fb_a, fb_c) || !same_levels(fa_a, fa_c)) mismatch_c++;
            if (total_qty(fb_a) + total_qty(fa_a) != expected_total) qty_bad_a++;
            if (total_qty(fb_b) + total_qty(fa_b) != expected_total) qty_bad_b++;
            if (total_qty(fb_c) + total_qty(fa_c) != expected_total) qty_bad_c++;
        }
    }

    bool pass_a = edge_a && qty_bad_a == 0;
    bool pass_b = edge_b && qty_bad_b == 0 && mismatch_b == 0;
    bool pass_c = edge_c && qty_bad_c == 0 && mismatch_c == 0;

    std::printf("bbo checks done: %ld\n", checks);
    std::printf("mismatches vs A: B = %ld, C = %ld\n", mismatch_b, mismatch_c);
    std::printf("total qty wrong: A = %ld, B = %ld, C = %ld\n\n", qty_bad_a, qty_bad_b, qty_bad_c);

    std::printf("Implementation A: %s\n", pass_a ? "PASS" : "FAIL");
    std::printf("Implementation B: %s\n", pass_b ? "PASS" : "FAIL");
    std::printf("Implementation C: %s\n\n", pass_c ? "PASS" : "FAIL");

    print_bbo(a->get_bbo());
    std::printf("\n");

    delete a;
    delete b;
    delete c;

    if (pass_a && pass_b && pass_c) {
        std::printf("All implementations produce identical results.\n");
        return 0;
    }
    std::printf("Implementations do NOT agree.\n");
    return 1;
}

// ---------------------------------------------------------------------------

void usage() {
    std::printf("usage:\n");
    std::printf("  ./bench --verify\n");
    std::printf("  ./bench --impl A|B|C [--runs N]\n");
}

int main(int argc, char** argv) {
    std::string impl = "";
    bool verify = false;
    int runs = 5;

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--verify") {
            verify = true;
        } else if (arg == "--impl" && i + 1 < argc) {
            impl = argv[++i];
        } else if (arg == "--runs" && i + 1 < argc) {
            runs = std::stoi(argv[++i]);
        } else {
            usage();
            return 1;
        }
    }

    if (verify) {
        return run_verify();
    }
    if (runs < 1) {
        runs = 1;
    }

    std::vector<Op> ops = make_workload(kNumOps, kSeed);
    if (impl == "A") {
        run_bench<MapOrderBook>("A (std::map)", ops, runs);
    } else if (impl == "B") {
        run_bench<HeapOrderBook>("B (priority_queue + lazy deletion)", ops, runs);
    } else if (impl == "C") {
        run_bench<ArrayOrderBook>("C (flat price array + order pool)", ops, runs);
    } else {
        usage();
        return 1;
    }
    return 0;
}

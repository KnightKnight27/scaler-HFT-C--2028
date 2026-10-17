#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <random>
#include <string>
#include <vector>

#include "array_order_book.hpp"
#include "heap_order_book.hpp"
#include "map_order_book.hpp"

using Clock = std::chrono::steady_clock;

const int NUM_OPS = 1'000'000;
const int RUNS = 5;
const unsigned SEED = 42;

enum class OpType { Add, Cancel, Bbo };

struct Op {
    OpType type;
    Side side;
    uint64_t id;
    double price;
    long qty;
};

// addPct% add, cancel up to 90%, 10% bbo (the benchmark uses 70 / 20 / 10)
// rng() % n instead of uniform_int_distribution so mac and linux get the same ops
std::vector<Op> makeWorkload(int n, int band, unsigned addPct) {
    std::mt19937 rng(SEED);
    std::vector<Op> ops;
    std::vector<uint64_t> live;   // ids that can still be cancelled
    ops.reserve(n);
    uint64_t nextId = 1;

    for (int i = 0; i < n; i++) {
        unsigned r = rng() % 100;
        if (r < addPct || live.empty()) {
            Side side = (rng() % 2 == 0) ? Side::Buy : Side::Sell;
            int offset = rng() % band;
            int tick = (side == Side::Buy) ? MAX_BID - offset : MIN_ASK + offset;
            long qty = 1 + rng() % 100;
            ops.push_back({OpType::Add, side, nextId, toPrice(tick), qty});
            live.push_back(nextId++);
        } else if (r < 90) {
            size_t k = rng() % live.size();
            ops.push_back({OpType::Cancel, Side::Buy, live[k], 0, 0});
            live[k] = live.back();
            live.pop_back();
        } else {
            ops.push_back({OpType::Bbo, Side::Buy, 0, 0, 0});
        }
    }
    return ops;
}

template <typename Book>
void runOp(Book& book, const Op& op, long& sum) {
    if (op.type == OpType::Add) {
        book.add_order(op.id, op.side, op.price, op.qty);
    } else if (op.type == OpType::Cancel) {
        sum += book.cancel_order(op.id);
    } else {
        BBO b = book.get_bbo();
        sum += b.bid_qty + b.ask_qty + toTick(b.bid_price) + toTick(b.ask_price);
    }
}

template <typename Book>
void runDepth(const Book& book, long& sum) {
    for (const Level& l : book.get_bids(10)) sum += l.qty + toTick(l.price);
    for (const Level& l : book.get_asks(10)) sum += l.qty + toTick(l.price);
}

struct Result {
    double totalMs = 0, addNs = 0, cancelNs = 0, bboNs = 0;
    long checksum = 0;
    BBO finalBBO{};
};

double nsBetween(Clock::time_point a, Clock::time_point b) {
    return std::chrono::duration<double, std::nano>(b - a).count();
}

// pass 1: no timer inside the loop, only the total time
template <typename Book>
void runUntimed(const std::vector<Op>& ops, Result& r) {
    Book book;
    long sum = 0;
    auto start = Clock::now();
    for (size_t i = 0; i < ops.size(); i++) {
        runOp(book, ops[i], sum);
        if ((i + 1) % 100 == 0) runDepth(book, sum);
    }
    r.totalMs = nsBetween(start, Clock::now()) / 1e6;
    r.checksum = sum;
    r.finalBBO = book.get_bbo();
}

// pass 2: same ops again, timer around every op
template <typename Book>
void runTimed(const std::vector<Op>& ops, Result& r) {
    Book book;
    long sum = 0;
    double ns[3] = {0, 0, 0};
    long cnt[3] = {0, 0, 0};
    for (size_t i = 0; i < ops.size(); i++) {
        auto t0 = Clock::now();
        runOp(book, ops[i], sum);
        auto t1 = Clock::now();
        int k = (int)ops[i].type;
        ns[k] += nsBetween(t0, t1);
        cnt[k]++;
        if ((i + 1) % 100 == 0) runDepth(book, sum);
    }
    if (sum != r.checksum) printf("warning: checksum is different in the timed run\n");
    r.addNs = ns[0] / cnt[0];
    r.cancelNs = ns[1] / cnt[1];
    r.bboNs = ns[2] / cnt[2];
}

double clockCost() {
    double total = 0;
    for (int i = 0; i < 1'000'000; i++) {
        auto t0 = Clock::now();
        auto t1 = Clock::now();
        total += nsBetween(t0, t1);
    }
    return total / 1'000'000;
}

double median(std::vector<double> v) {
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

void printBBO(const BBO& b) {
    printf("Final BBO:\n");
    if (b.has_bid) printf("Bid: %.2f x %ld\n", b.bid_price, b.bid_qty);
    else printf("Bid: none\n");
    if (b.has_ask) printf("Ask: %.2f x %ld\n", b.ask_price, b.ask_qty);
    else printf("Ask: none\n");
}

template <typename Book>
void bench(const char* name, bool perfMode) {
    std::vector<Op> ops = makeWorkload(NUM_OPS, 5000, 70);
    printf("Implementation %s\n", name);

    Result r;
    if (perfMode) {   // one run without per-op timers, used under perf / cachegrind
        runUntimed<Book>(ops, r);
        printf("total: %.2f ms\n", r.totalMs);
        printBBO(r.finalBBO);
        printf("Checksum: %ld\n", r.checksum);
        return;
    }

    long cnt[3] = {0, 0, 0};
    for (const Op& op : ops) cnt[(int)op.type]++;
    printf("%d ops: %ld add, %ld cancel, %ld bbo, get_bids(10) + get_asks(10) every 100 ops, seed %u\n\n",
           NUM_OPS, cnt[0], cnt[1], cnt[2], SEED);

    std::vector<double> total, add, cancel, bbo;
    for (int run = 1; run <= RUNS; run++) {
        runUntimed<Book>(ops, r);
        runTimed<Book>(ops, r);
        printf("run %d:  total %7.2f ms | add %6.1f ns | cancel %6.1f ns | bbo %5.1f ns\n",
               run, r.totalMs, r.addNs, r.cancelNs, r.bboNs);
        total.push_back(r.totalMs);
        add.push_back(r.addNs);
        cancel.push_back(r.cancelNs);
        bbo.push_back(r.bboNs);
    }
    printf("median: total %7.2f ms | add %6.1f ns | cancel %6.1f ns | bbo %5.1f ns\n",
           median(total), median(add), median(cancel), median(bbo));
    printf("one clock read costs %.1f ns, it is included in add / cancel / bbo\n\n", clockCost());
    printBBO(r.finalBBO);
    printf("Checksum: %ld\n", r.checksum);
}

bool sameBBO(const BBO& a, const BBO& b) {
    return a.has_bid == b.has_bid && a.has_ask == b.has_ask &&
           (!a.has_bid || (a.bid_price == b.bid_price && a.bid_qty == b.bid_qty)) &&
           (!a.has_ask || (a.ask_price == b.ask_price && a.ask_qty == b.ask_qty));
}

bool sameLevels(const std::vector<Level>& a, const std::vector<Level>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); i++)
        if (a[i].price != b[i].price || a[i].qty != b[i].qty) return false;
    return true;
}

struct State {
    int cancelResult;   // -1 if the op was not a cancel
    BBO bbo;
    std::vector<Level> bids, asks;
};

bool sameState(const State& a, const State& b) {
    return a.cancelResult == b.cancelResult && sameBBO(a.bbo, b.bbo) &&
           sameLevels(a.bids, b.bids) && sameLevels(a.asks, b.asks);
}

template <typename Book>
State applyOp(Book& book, const Op& op, int depth) {
    State s{-1, {}, {}, {}};
    if (op.type == OpType::Add) book.add_order(op.id, op.side, op.price, op.qty);
    if (op.type == OpType::Cancel) s.cancelResult = book.cancel_order(op.id);
    s.bbo = book.get_bbo();
    s.bids = book.get_bids(depth);
    s.asks = book.get_asks(depth);
    return s;
}

// A, B and C get the same ops, B and C are checked against A after every op
void compareBooks(const std::vector<Op>& ops, long& badB, long& badC) {
    MapOrderBook a;
    HeapOrderBook b;
    ArrayOrderBook c;
    for (size_t i = 0; i < ops.size(); i++) {
        int depth = ((i + 1) % 10000 == 0) ? 10000 : 10;   // full book every 10000 ops
        State sa = applyOp(a, ops[i], depth);
        if (!sameState(sa, applyOp(b, ops[i], depth))) badB++;
        if (!sameState(sa, applyOp(c, ops[i], depth))) badC++;
    }
}

template <typename Book>
bool edgeCases() {
    Book book;
    bool ok = true;
    auto check = [&](bool cond, const char* what) {
        if (!cond) {
            printf("    failed: %s\n", what);
            ok = false;
        }
    };

    check(!book.get_bbo().has_bid && !book.get_bbo().has_ask, "empty book bbo");
    check(book.get_bids(10).empty() && book.get_asks(10).empty(), "empty book levels");
    check(!book.cancel_order(999), "cancel unknown id");

    book.add_order(1, Side::Buy, 99.50, 10);
    book.add_order(2, Side::Buy, 99.50, 5);
    book.add_order(3, Side::Buy, 99.00, 7);
    book.add_order(4, Side::Buy, 98.00, 1);
    book.add_order(5, Side::Sell, 100.50, 3);

    std::vector<Level> bids = book.get_bids(10);
    check(bids.size() == 3, "get_bids(10) with only 3 levels");
    check(bids.size() == 3 && bids[0].price == 99.50 && bids[0].qty == 15 && bids[1].price == 99.00 &&
              bids[2].price == 98.00,
          "bid levels order / qty");
    check(book.get_asks(10).size() == 1, "get_asks(10) with only 1 level");

    check(book.cancel_order(1), "cancel live order");
    check(!book.cancel_order(1), "cancel same order twice");
    check(book.get_bbo().bid_qty == 5, "qty after cancel");

    book.cancel_order(3);   // 99.00 is empty now, but it is not the best level
    check(book.get_bids(10).size() == 2, "empty level removed");
    book.add_order(6, Side::Buy, 99.00, 2);
    bids = book.get_bids(10);
    check(bids.size() == 3 && bids[1].price == 99.00 && bids[1].qty == 2, "add again at an emptied price");

    book.cancel_order(2);   // best bid 99.50 is empty now
    BBO b = book.get_bbo();
    check(b.has_bid && b.bid_price == 99.00 && b.bid_qty == 2, "best bid moves down");
    book.cancel_order(6);
    book.cancel_order(4);
    check(!book.get_bbo().has_bid && book.get_bbo().has_ask, "no bids left");

    book.add_order(7, Side::Sell, 100.01, 4);
    b = book.get_bbo();
    check(b.ask_price == 100.01 && b.ask_qty == 4, "better ask");
    book.cancel_order(7);
    check(book.get_bbo().ask_price == 100.50, "best ask moves up");

    book.add_order(8, Side::Buy, 100.50, 1);   // bid in the ask range
    book.add_order(9, Side::Sell, 99.00, 1);   // ask in the bid range
    book.add_order(10, Side::Buy, 99.00, 0);   // qty 0
    check(!book.cancel_order(8) && !book.cancel_order(9) && !book.cancel_order(10), "invalid orders ignored");
    return ok;
}

int verify() {
    printf("Running correctness test...\n\n");

    bool edgeA = edgeCases<MapOrderBook>();
    bool edgeB = edgeCases<HeapOrderBook>();
    bool edgeC = edgeCases<ArrayOrderBook>();
    printf("edge cases: A %s, B %s, C %s\n", edgeA ? "ok" : "FAILED", edgeB ? "ok" : "FAILED", edgeC ? "ok" : "FAILED");

    long badB = 0, badC = 0;
    printf("workload 1: %d ops, seed %u, 70%% add / 20%% cancel, full price range (same as the benchmark)\n",
           NUM_OPS, SEED);
    compareBooks(makeWorkload(NUM_OPS, 5000, 70), badB, badC);
    printf("workload 2: 200000 ops, seed %u, 40%% add / 50%% cancel, prices within 20 ticks of the mid\n", SEED);
    printf("            (book stays small, best levels keep getting empty, sometimes a whole side is empty)\n");
    compareBooks(makeWorkload(200000, 20, 40), badB, badC);
    printf("after every op B and C are compared with A: cancel result, bbo, top 10 bids and asks\n");
    printf("(full book every 10000 ops)\n");
    printf("mismatches: B %ld, C %ld\n\n", badB, badC);

    bool okA = edgeA, okB = edgeB && badB == 0, okC = edgeC && badC == 0;
    printf("Implementation A: %s\n", okA ? "PASS" : "FAIL");
    printf("Implementation B: %s\n", okB ? "PASS" : "FAIL");
    printf("Implementation C: %s\n", okC ? "PASS" : "FAIL");
    if (okA && okB && okC) printf("\nAll implementations produce identical results.\n");
    else printf("\nImplementations do not agree.\n");
    return (okA && okB && okC) ? 0 : 1;
}

int main(int argc, char** argv) {
    std::string impl;
    bool doVerify = false, perfMode = false;
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--verify") doVerify = true;
        else if (arg == "--perf") perfMode = true;
        else if (arg == "--impl" && i + 1 < argc) impl = argv[++i];
    }

    if (doVerify) return verify();

    if (impl == "A") {
        bench<MapOrderBook>("A (std::map + std::list)", perfMode);
    } else if (impl == "B") {
        bench<HeapOrderBook>("B (priority_queue + lazy deletion)", perfMode);
    } else if (impl == "C") {
        bench<ArrayOrderBook>("C (array indexed by price)", perfMode);
    } else if (impl == "none") {   // only makes the workload, baseline for perf
        std::vector<Op> ops = makeWorkload(NUM_OPS, 5000, 70);
        long adds = 0;
        for (const Op& op : ops) adds += (op.type == OpType::Add);
        printf("workload only: %zu ops, %ld adds\n", ops.size(), adds);
    } else {
        printf("usage: ./bench --verify\n");
        printf("       ./bench --impl A|B|C\n");
        printf("       ./bench --impl A|B|C|none --perf\n");
        return 1;
    }
    return 0;
}

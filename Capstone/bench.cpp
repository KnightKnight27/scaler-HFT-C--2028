// bench.cpp: one benchmark and correctness test for all three order books.
//
// Build:  g++ -std=c++20 -O2 -Wall -o bench bench.cpp
//
// Run:    ./bench --impl A            5 runs, prints every run and the median
//         ./bench --impl A --runs 1   a single run
//         ./bench --impl A --perf     one plain pass, no per-call timers (use this under perf stat)
//         ./bench --baseline          builds the workload and exits: the harness cost to subtract
//                                     from each --perf run's perf counters
//         ./bench --verify            same workload through A, B and C, compared after every op

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include "types.hpp"
#include "book_a.hpp"
#include "book_b.hpp"
#include "book_c.hpp"

using Clock = std::chrono::steady_clock;

constexpr int kNumOps     = 1'000'000;
constexpr int kDepth      = 10;     // get_bids(10) and get_asks(10) ...
constexpr int kDepthEvery = 100;    // ... every 100 operations
constexpr int kBidMin = 5000,  kBidMax = 9999;     // 50.00 .. 99.99 in ticks
constexpr int kAskMin = 10001, kAskMax = 15000;    // 100.01 .. 150.00 in ticks

// ---------------------------------------------------------------------------
// Workload: generated once, before any timing, then replayed on every book.
// ---------------------------------------------------------------------------

enum class OpType { Add, Cancel, Bbo };

struct Op {
    uint64_t id;      // Add, Cancel
    int price;        // Add (ticks)
    int qty;          // Add
    Side side;        // Add
    OpType type;
};

std::vector<Op> make_workload() {
    std::mt19937 rng(42);
    std::uniform_int_distribution<int> percent(0, 99);
    std::uniform_int_distribution<int> coin(0, 1);
    std::uniform_int_distribution<int> qty_dist(1, 100);
    std::uniform_int_distribution<int> bid_dist(kBidMin, kBidMax);
    std::uniform_int_distribution<int> ask_dist(kAskMin, kAskMax);

    std::vector<Op> ops;
    ops.reserve(kNumOps);

    // ids of the orders resting right now, so every cancel picks a live order
    std::vector<uint64_t> live;
    live.reserve(kNumOps);
    uint64_t next_id = 1;

    for (int i = 0; i < kNumOps; i++) {
        int p = percent(rng);
        Op op{};

        if (p < 70 || (p < 90 && live.empty())) {       // 70% add (also when nothing is left to cancel)
            op.type = OpType::Add;
            op.id = next_id++;
            op.side = coin(rng) ? Side::Buy : Side::Sell;
            op.price = (op.side == Side::Buy) ? bid_dist(rng) : ask_dist(rng);
            op.qty = qty_dist(rng);
            live.push_back(op.id);
        } else if (p < 90) {                            // 20% cancel a random live order
            op.type = OpType::Cancel;
            std::uniform_int_distribution<size_t> pick(0, live.size() - 1);
            size_t k = pick(rng);
            op.id = live[k];
            live[k] = live.back();                      // O(1) remove: move the last id into the hole
            live.pop_back();
        } else {                                        // 10% BBO query
            op.type = OpType::Bbo;
        }
        ops.push_back(op);
    }
    return ops;
}

void print_workload_summary(const std::vector<Op>& ops) {
    long adds = 0, cancels = 0, bbos = 0;
    for (const Op& op : ops) {
        if (op.type == OpType::Add) adds++;
        else if (op.type == OpType::Cancel) cancels++;
        else bbos++;
    }
    printf("Workload: %zu ops, seed 42: %ld add, %ld cancel, %ld BBO, "
           "get_bids/get_asks(%d) every %d ops\n\n",
           ops.size(), adds, cancels, bbos, kDepth, kDepthEvery);
}

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------

// Every query result is folded into one checksum. Printing it stops the compiler
// from deleting the calls, and it must come out identical for A, B and C.
uint64_t bbo_sum(const BBO& b) {
    uint64_t s = 0;
    if (b.has_bid) s += b.bid_price + b.bid_qty;
    if (b.has_ask) s += b.ask_price + b.ask_qty;
    return s;
}

uint64_t levels_sum(const std::vector<Level>& levels) {
    uint64_t s = 0;
    for (const Level& l : levels) s += l.price + l.qty;
    return s;
}

long long ns_between(Clock::time_point t0, Clock::time_point t1) {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
}

double median(std::vector<double> v) {
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

// Ticks are only turned back into a decimal here, for printing.
void print_price(int ticks) {
    printf("%d.%02d", ticks / 100, ticks % 100);
}

void print_bbo(const BBO& b) {
    printf("  Bid: ");
    if (b.has_bid) { print_price(b.bid_price); printf(" x %ld\n", b.bid_qty); }
    else printf("none\n");
    printf("  Ask: ");
    if (b.has_ask) { print_price(b.ask_price); printf(" x %ld\n", b.ask_qty); }
    else printf("none\n");
}

// ---------------------------------------------------------------------------
// Benchmark
// ---------------------------------------------------------------------------

struct RunResult {
    double total_ms;
    double add_ns, cancel_ns, bbo_ns, depth_ns;
    uint64_t checksum;
    BBO final_bbo;
};

// Cost of a Clock::now() pair with nothing between them. Every per-call timing
// below includes this, so it is subtracted from the per-call averages.
double clock_overhead_ns() {
    const int n = 1'000'000;
    long long total = 0;
    for (int i = 0; i < n; i++) {
        Clock::time_point t0 = Clock::now();
        Clock::time_point t1 = Clock::now();
        total += ns_between(t0, t1);
    }
    return double(total) / n;
}

// Pass 1: the whole workload under a single timer -> total time and checksum.
template <class Book>
void total_pass(const std::vector<Op>& ops, RunResult& r) {
    std::unique_ptr<Book> book(new Book());    // on the heap: a book can be too big for the stack
    uint64_t sum = 0;

    Clock::time_point t0 = Clock::now();
    for (size_t i = 0; i < ops.size(); i++) {
        const Op& op = ops[i];
        if (op.type == OpType::Add) {
            book->add_order(op.id, op.side, op.price, op.qty);
        } else if (op.type == OpType::Cancel) {
            sum += book->cancel_order(op.id);
        } else {
            sum += bbo_sum(book->get_bbo());
        }
        if ((i + 1) % kDepthEvery == 0) {
            sum += levels_sum(book->get_bids(kDepth));
            sum += levels_sum(book->get_asks(kDepth));
        }
    }
    Clock::time_point t1 = Clock::now();

    r.total_ms = ns_between(t0, t1) / 1e6;
    r.checksum = sum;
    r.final_bbo = book->get_bbo();
}

// Pass 2: the same workload on a fresh book, with every call timed on its own.
template <class Book>
void per_call_pass(const std::vector<Op>& ops, double clock_ns, RunResult& r) {
    std::unique_ptr<Book> book(new Book());
    long long add_total = 0, cancel_total = 0, bbo_total = 0, depth_total = 0;
    long long adds = 0, cancels = 0, bbos = 0, depths = 0;
    uint64_t sum = 0;

    for (size_t i = 0; i < ops.size(); i++) {
        const Op& op = ops[i];
        if (op.type == OpType::Add) {
            Clock::time_point t0 = Clock::now();
            book->add_order(op.id, op.side, op.price, op.qty);
            Clock::time_point t1 = Clock::now();
            add_total += ns_between(t0, t1);
            adds++;
        } else if (op.type == OpType::Cancel) {
            Clock::time_point t0 = Clock::now();
            bool ok = book->cancel_order(op.id);
            Clock::time_point t1 = Clock::now();
            cancel_total += ns_between(t0, t1);
            cancels++;
            sum += ok;
        } else {
            Clock::time_point t0 = Clock::now();
            BBO b = book->get_bbo();
            Clock::time_point t1 = Clock::now();
            bbo_total += ns_between(t0, t1);
            bbos++;
            sum += bbo_sum(b);
        }
        if ((i + 1) % kDepthEvery == 0) {
            Clock::time_point t0 = Clock::now();
            std::vector<Level> bids = book->get_bids(kDepth);
            std::vector<Level> asks = book->get_asks(kDepth);
            Clock::time_point t1 = Clock::now();
            depth_total += ns_between(t0, t1);
            depths++;
            sum += levels_sum(bids) + levels_sum(asks);
        }
    }

    r.add_ns    = double(add_total) / adds - clock_ns;
    r.cancel_ns = double(cancel_total) / cancels - clock_ns;
    r.bbo_ns    = double(bbo_total) / bbos - clock_ns;
    r.depth_ns  = double(depth_total) / depths - clock_ns;
    if (sum != r.checksum) printf("  warning: pass 2 checksum differs from pass 1\n");
}

template <class Book>
void run_bench(const char* name, const std::vector<Op>& ops, int runs) {
    double clock_ns = clock_overhead_ns();
    std::vector<double> total, add, cancel, bbo, depth;
    RunResult r{};

    printf("Implementation %s, %d run(s)\n", name, runs);
    for (int run = 1; run <= runs; run++) {
        total_pass<Book>(ops, r);
        per_call_pass<Book>(ops, clock_ns, r);
        total.push_back(r.total_ms);
        add.push_back(r.add_ns);
        cancel.push_back(r.cancel_ns);
        bbo.push_back(r.bbo_ns);
        depth.push_back(r.depth_ns);
        printf("  run %d: total %8.2f ms | add %6.1f ns | cancel %6.1f ns | BBO %6.1f ns | bids+asks %7.1f ns\n",
               run, r.total_ms, r.add_ns, r.cancel_ns, r.bbo_ns, r.depth_ns);
    }

    printf("\nMedian of %d run(s)\n", runs);
    printf("  Total time       : %.2f ms\n", median(total));
    printf("  ns / add_order   : %.1f\n", median(add));
    printf("  ns / cancel_order: %.1f\n", median(cancel));
    printf("  ns / get_bbo     : %.1f\n", median(bbo));
    printf("  ns / bids+asks   : %.1f   (get_bids(%d) + get_asks(%d) together)\n",
           median(depth), kDepth, kDepth);
    printf("  (per-call numbers have the %.1f ns clock overhead subtracted)\n", clock_ns);

    printf("\nFinal BBO:\n");
    print_bbo(r.final_bbo);
    printf("\nChecksum: %llu\n", (unsigned long long)r.checksum);
}

// One plain pass and nothing else, so perf stat counts the order book and not
// a million Clock::now() calls.
template <class Book>
void run_perf(const char* name, const std::vector<Op>& ops) {
    RunResult r{};
    total_pass<Book>(ops, r);
    printf("Implementation %s: total %.2f ms\n", name, r.total_ms);
    printf("Final BBO:\n");
    print_bbo(r.final_bbo);
    printf("Checksum: %llu\n", (unsigned long long)r.checksum);
}

// ---------------------------------------------------------------------------
// Correctness test
// ---------------------------------------------------------------------------

struct Snapshot {
    BBO bbo;
    std::vector<Level> bids;
    std::vector<Level> asks;
};

template <class Book>
Snapshot snapshot(Book& book) {
    Snapshot s;
    s.bbo = book.get_bbo();
    s.bids = book.get_bids(kDepth);
    s.asks = book.get_asks(kDepth);
    return s;
}

bool same_levels(const std::vector<Level>& x, const std::vector<Level>& y) {
    if (x.size() != y.size()) return false;
    for (size_t i = 0; i < x.size(); i++) {
        if (x[i].price != y[i].price || x[i].qty != y[i].qty) return false;
    }
    return true;
}

bool same_snapshot(const Snapshot& x, const Snapshot& y) {
    const BBO& a = x.bbo;
    const BBO& b = y.bbo;
    if (a.has_bid != b.has_bid || a.has_ask != b.has_ask) return false;
    if (a.has_bid && (a.bid_price != b.bid_price || a.bid_qty != b.bid_qty)) return false;
    if (a.has_ask && (a.ask_price != b.ask_price || a.ask_qty != b.ask_qty)) return false;
    return same_levels(x.bids, y.bids) && same_levels(x.asks, y.asks);
}

// Checks one book on its own. B and C are compared against A, so this is what
// catches a bug in A itself. Returns nullptr if fine, else what is wrong.
const char* sanity_problem(const Snapshot& s) {
    if (s.bids.size() > kDepth || s.asks.size() > kDepth) return "returned more levels than asked for";
    if (s.bbo.has_bid != !s.bids.empty()) return "has_bid disagrees with get_bids";
    if (s.bbo.has_ask != !s.asks.empty()) return "has_ask disagrees with get_asks";
    if (s.bbo.has_bid && (s.bbo.bid_price != s.bids[0].price || s.bbo.bid_qty != s.bids[0].qty))
        return "BBO bid is not the first level of get_bids";
    if (s.bbo.has_ask && (s.bbo.ask_price != s.asks[0].price || s.bbo.ask_qty != s.asks[0].qty))
        return "BBO ask is not the first level of get_asks";

    for (size_t i = 0; i < s.bids.size(); i++) {
        if (s.bids[i].qty <= 0) return "a bid level has quantity <= 0";
        if (s.bids[i].price < kBidMin || s.bids[i].price > kBidMax) return "a bid price is out of range";
        if (i > 0 && s.bids[i].price >= s.bids[i - 1].price) return "bids are not strictly descending";
    }
    for (size_t i = 0; i < s.asks.size(); i++) {
        if (s.asks[i].qty <= 0) return "an ask level has quantity <= 0";
        if (s.asks[i].price < kAskMin || s.asks[i].price > kAskMax) return "an ask price is out of range";
        if (i > 0 && s.asks[i].price <= s.asks[i - 1].price) return "asks are not strictly ascending";
    }
    return nullptr;
}

struct Status {
    bool ok = true;
    long op_index = -1;     // first failing operation; -1 means the edge-case test
    std::string why;
};

void record_failure(Status& st, long op_index, const std::string& why) {
    if (!st.ok) return;     // keep only the first failure
    st.ok = false;
    st.op_index = op_index;
    st.why = why;
}

// The "Required Behavior" cases from the handout, on a fresh book.
template <class Book>
void check_edge_cases(Status& st) {
    std::unique_ptr<Book> book(new Book());

    BBO b = book->get_bbo();
    if (b.has_bid || b.has_ask) record_failure(st, -1, "empty book reports a bid or an ask");
    if (!book->get_bids(kDepth).empty() || !book->get_asks(kDepth).empty())
        record_failure(st, -1, "empty book returns levels");
    if (book->cancel_order(999)) record_failure(st, -1, "cancel of an unknown id returned true");

    book->add_order(1, Side::Buy, 9000, 10);
    book->add_order(2, Side::Buy, 9100, 20);
    book->add_order(3, Side::Buy, 9200, 30);
    book->add_order(4, Side::Buy, 9200, 5);     // same price as id 3, so level 92.00 holds 35
    std::vector<Level> bids = book->get_bids(10);
    if (bids.size() != 3) record_failure(st, -1, "get_bids(10) with 3 levels did not return 3");
    else if (bids[0].price != 9200 || bids[0].qty != 35)
        record_failure(st, -1, "quantities at one price are not summed");

    if (!book->cancel_order(3)) record_failure(st, -1, "cancel of a live order returned false");
    if (book->cancel_order(3)) record_failure(st, -1, "cancel of an already-cancelled order returned true");

    book->cancel_order(4);                      // 92.00 is now empty, so the best bid must move to 91.00
    b = book->get_bbo();
    if (!b.has_bid || b.bid_price != 9100)
        record_failure(st, -1, "best bid did not move when its level emptied");
}

template <class Book>
void apply_checked(Book& book, const Op& op, long i, Status& st) {
    if (op.type == OpType::Add) {
        book.add_order(op.id, op.side, op.price, op.qty);
    } else if (op.type == OpType::Cancel) {
        if (!book.cancel_order(op.id)) record_failure(st, i, "cancel of a live order returned false");
    }
    // A Bbo op changes nothing; the snapshot taken after every op checks the BBO.
}

void print_status(const char* name, const Status& st, const std::vector<Op>& ops) {
    if (st.ok) {
        printf("Implementation %s: PASS\n", name);
        return;
    }
    if (st.op_index < 0) {
        printf("Implementation %s: FAIL in edge-case test: %s\n", name, st.why.c_str());
        return;
    }
    const Op& op = ops[st.op_index];
    printf("Implementation %s: FAIL after operation %ld (", name, st.op_index);
    if (op.type == OpType::Add) {
        printf("add id %llu %s ", (unsigned long long)op.id, op.side == Side::Buy ? "buy" : "sell");
        print_price(op.price);
        printf(" x %d", op.qty);
    } else if (op.type == OpType::Cancel) {
        printf("cancel id %llu", (unsigned long long)op.id);
    } else {
        printf("get_bbo");
    }
    printf("): %s\n", st.why.c_str());
}

bool run_verify(const std::vector<Op>& ops) {
    printf("Running correctness test...\n\n");
    Status sa, sb, sc;
    check_edge_cases<BookA>(sa);
    check_edge_cases<BookB>(sb);
    check_edge_cases<BookC>(sc);

    std::unique_ptr<BookA> a(new BookA());
    std::unique_ptr<BookB> b(new BookB());
    std::unique_ptr<BookC> c(new BookC());

    for (size_t i = 0; i < ops.size(); i++) {
        apply_checked(*a, ops[i], i, sa);
        apply_checked(*b, ops[i], i, sb);
        apply_checked(*c, ops[i], i, sc);

        Snapshot snap_a = snapshot(*a);
        Snapshot snap_b = snapshot(*b);
        Snapshot snap_c = snapshot(*c);

        const char* why = sanity_problem(snap_a);
        if (why) record_failure(sa, i, why);
        why = sanity_problem(snap_b);
        if (why) record_failure(sb, i, why);
        why = sanity_problem(snap_c);
        if (why) record_failure(sc, i, why);

        if (!same_snapshot(snap_b, snap_a)) record_failure(sb, i, "BBO or levels differ from A");
        if (!same_snapshot(snap_c, snap_a)) record_failure(sc, i, "BBO or levels differ from A");

        if (!sa.ok && !sb.ok && !sc.ok) break;  // all failed, nothing more to learn
    }

    print_status("A", sa, ops);
    print_status("B", sb, ops);
    print_status("C", sc, ops);

    bool all_ok = sa.ok && sb.ok && sc.ok;
    if (all_ok) printf("\nAll implementations produce identical results.\n");
    else printf("\nSome implementations FAILED. B and C are compared against A, so fix A first.\n");
    return all_ok;
}

// ---------------------------------------------------------------------------

void usage() {
    printf("usage: ./bench --impl A|B|C [--runs N] [--perf]\n");
    printf("       ./bench --verify\n");
    printf("       ./bench --baseline\n");
}

int main(int argc, char** argv) {
    std::string impl;
    bool verify = false;
    bool perf = false;
    bool baseline = false;
    int runs = 5;

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--impl" && i + 1 < argc) impl = argv[++i];
        else if (arg == "--runs" && i + 1 < argc) runs = std::atoi(argv[++i]);
        else if (arg == "--verify") verify = true;
        else if (arg == "--perf") perf = true;
        else if (arg == "--baseline") baseline = true;
        else { usage(); return 1; }
    }
    if (!verify && !baseline && impl.empty()) { usage(); return 1; }
    if (runs < 1) runs = 1;

    std::vector<Op> ops = make_workload();
    print_workload_summary(ops);

    // Building the workload is part of every run. Its perf counters, measured
    // alone here, are subtracted from each --perf run to leave the book's share.
    if (baseline) return 0;

    if (verify) return run_verify(ops) ? 0 : 1;

    if (impl == "A") {
        if (perf) run_perf<BookA>("A", ops); else run_bench<BookA>("A", ops, runs);
    } else if (impl == "B") {
        if (perf) run_perf<BookB>("B", ops); else run_bench<BookB>("B", ops, runs);
    } else if (impl == "C") {
        if (perf) run_perf<BookC>("C", ops); else run_bench<BookC>("C", ops, runs);
    } else {
        usage();
        return 1;
    }
    return 0;
}

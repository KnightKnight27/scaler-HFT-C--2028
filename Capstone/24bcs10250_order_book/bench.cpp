#include <algorithm>
#include <chrono>
#include <cstdio>
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

// Builds the whole workload before timing starts.
// rng() % n is used instead of uniform_int_distribution so the
// same seed gives the same ops on mac (libc++) and linux (libstdc++).
std::vector<Op> make_workload(int n, uint32_t seed) {
    std::mt19937 rng(seed);
    std::vector<Op> ops;
    ops.reserve(n);
    std::vector<uint64_t> live;
    uint64_t next_id = 1;

    for (int i = 0; i < n; i++) {
        uint32_t r = rng() % 100;

        if (r >= 70 && r < 90 && !live.empty()) {
            size_t k = rng() % live.size();
            ops.push_back({OpType::Cancel, live[k], Side::Buy, 0, 0});
            live[k] = live.back();
            live.pop_back();
        } else if (r >= 90) {
            ops.push_back({OpType::Bbo, 0, Side::Buy, 0, 0});
        } else {
            Side side = (rng() % 2 == 0) ? Side::Buy : Side::Sell;
            Price price = (side == Side::Buy) ? BID_LOW + rng() % LEVELS_PER_SIDE
                                              : ASK_LOW + rng() % LEVELS_PER_SIDE;
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

struct RunResult {
    double total_ms = 0;
    double add_ns = 0;
    double cancel_ns = 0;
    double bbo_ns = 0;
    uint64_t checksum = 0;
    BBO final_bbo;
};

long long ns_between(Clock::time_point a, Clock::time_point b) {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(b - a).count();
}

template <class Book>
RunResult run_once(const std::vector<Op>& ops) {
    Book book;
    RunResult res;
    uint64_t sum = 0;
    long long add_time = 0, cancel_time = 0, bbo_time = 0;
    long long adds = 0, cancels = 0, bbos = 0;

    auto start = Clock::now();
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
    auto end = Clock::now();

    res.total_ms = ns_between(start, end) / 1e6;
    res.add_ns = adds ? (double)add_time / adds : 0;
    res.cancel_ns = cancels ? (double)cancel_time / cancels : 0;
    res.bbo_ns = bbos ? (double)bbo_time / bbos : 0;
    res.final_bbo = book.get_bbo();
    mix_bbo(sum, res.final_bbo);
    res.checksum = sum;
    return res;
}

double median(std::vector<double> v) {
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

template <class Book>
void bench(const std::string& name, const std::vector<Op>& ops, int runs) {
    std::cout << "Implementation " << name << "\n";
    std::cout << "ops: " << ops.size() << ", runs: " << runs << "\n\n";

    std::vector<double> total, add, cancel, bbo;
    RunResult last;
    for (int r = 0; r < runs; r++) {
        last = run_once<Book>(ops);
        total.push_back(last.total_ms);
        add.push_back(last.add_ns);
        cancel.push_back(last.cancel_ns);
        bbo.push_back(last.bbo_ns);
        printf("run %d: total %.1f ms  add %.1f ns  cancel %.1f ns  bbo %.1f ns\n", r + 1,
               last.total_ms, last.add_ns, last.cancel_ns, last.bbo_ns);
    }

    printf("\nmedian of %d runs\n", runs);
    printf("total time  : %.1f ms\n", median(total));
    printf("ns / add    : %.1f\n", median(add));
    printf("ns / cancel : %.1f\n", median(cancel));
    printf("ns / bbo    : %.1f\n", median(bbo));

    const BBO& b = last.final_bbo;
    std::cout << "\nFinal BBO:\n";
    std::cout << "Bid: " << price_str(b.bid_price) << " x " << b.bid_qty << "\n";
    std::cout << "Ask: " << price_str(b.ask_price) << " x " << b.ask_qty << "\n";
    std::cout << "Checksum: " << last.checksum << "\n";
}

// ---------- correctness ----------

template <class Book>
bool edge_cases() {
    Book b;
    bool ok = true;
    auto check = [&](bool cond, const char* what) {
        if (!cond) {
            std::cout << "  failed: " << what << "\n";
            ok = false;
        }
    };

    BBO e = b.get_bbo();
    check(!e.has_bid && !e.has_ask, "empty book has no bid and no ask");
    check(b.get_bids(10).empty() && b.get_asks(10).empty(), "empty book has no levels");
    check(!b.cancel_order(42), "cancel unknown id returns false");

    b.add_order(1, Side::Buy, 9950, 10);
    b.add_order(2, Side::Buy, 9950, 5);
    b.add_order(3, Side::Buy, 9800, 7);
    b.add_order(4, Side::Sell, 10001, 20);
    b.add_order(5, Side::Sell, 10100, 3);

    BBO x = b.get_bbo();
    check(x.has_bid && x.bid_price == 9950 && x.bid_qty == 15, "qty at same price is summed");
    check(x.has_ask && x.ask_price == 10001 && x.ask_qty == 20, "best ask is lowest ask");
    check(same_levels(b.get_bids(10), {{9950, 15}, {9800, 7}}), "only 2 bid levels returned for depth 10");
    check(same_levels(b.get_asks(10), {{10001, 20}, {10100, 3}}), "asks sorted low to high");
    check(same_levels(b.get_bids(1), {{9950, 15}}), "depth 1 gives only the best level");

    check(b.cancel_order(1), "cancel live order returns true");
    check(!b.cancel_order(1), "cancel same order twice returns false");
    check(b.get_bbo().bid_qty == 5, "qty goes down after cancel");

    check(b.cancel_order(2), "cancel last order at best bid");
    check(b.get_bbo().bid_price == 9800, "next bid becomes best");
    check(b.cancel_order(4), "cancel best ask");
    check(b.get_bbo().ask_price == 10100, "next ask becomes best");

    b.cancel_order(3);
    b.cancel_order(5);
    BBO z = b.get_bbo();
    check(!z.has_bid && !z.has_ask, "book is empty again after all cancels");

    b.add_order(6, Side::Buy, 9800, 1);
    check(b.get_bbo().bid_price == 9800 && b.get_bbo().bid_qty == 1, "price level can come back after being emptied");

    return ok;
}

int verify(int n, uint32_t seed) {
    std::cout << "Running correctness test...\n\n";

    bool edge_a = edge_cases<MapBook>();
    bool edge_b = edge_cases<HeapBook>();
    bool edge_c = edge_cases<ArrayBook>();
    std::cout << "edge cases  A: " << (edge_a ? "ok" : "FAIL") << "  B: " << (edge_b ? "ok" : "FAIL")
              << "  C: " << (edge_c ? "ok" : "FAIL") << "\n";

    std::vector<Op> ops = make_workload(n, seed);
    MapBook a;
    HeapBook b;
    ArrayBook c;
    bool same_b = true, same_c = true;

    // A is used as the reference, B and C must give the same answer after every op
    for (size_t i = 0; i < ops.size(); i++) {
        const Op& op = ops[i];
        if (op.type == OpType::Add) {
            a.add_order(op.id, op.side, op.price, op.qty);
            b.add_order(op.id, op.side, op.price, op.qty);
            c.add_order(op.id, op.side, op.price, op.qty);
        } else if (op.type == OpType::Cancel) {
            bool ra = a.cancel_order(op.id);
            bool rb = b.cancel_order(op.id);
            bool rc = c.cancel_order(op.id);
            if (ra != rb) same_b = false;
            if (ra != rc) same_c = false;
        }

        BBO ba = a.get_bbo();
        if (!same_bbo(ba, b.get_bbo())) same_b = false;
        if (!same_bbo(ba, c.get_bbo())) same_c = false;

        if ((i + 1) % 100 == 0) {
            auto bids = a.get_bids(10), asks = a.get_asks(10);
            if (!same_levels(bids, b.get_bids(10)) || !same_levels(asks, b.get_asks(10))) same_b = false;
            if (!same_levels(bids, c.get_bids(10)) || !same_levels(asks, c.get_asks(10))) same_c = false;
        }

        if (!same_b || !same_c) {
            std::cout << "mismatch at op " << i << "\n";
            break;
        }
    }

    // full book at the end, every level on both sides
    auto all_bids = a.get_bids(LEVELS_PER_SIDE), all_asks = a.get_asks(LEVELS_PER_SIDE);
    if (!same_levels(all_bids, b.get_bids(LEVELS_PER_SIDE)) || !same_levels(all_asks, b.get_asks(LEVELS_PER_SIDE))) same_b = false;
    if (!same_levels(all_bids, c.get_bids(LEVELS_PER_SIDE)) || !same_levels(all_asks, c.get_asks(LEVELS_PER_SIDE))) same_c = false;

    std::cout << "random workload: " << ops.size() << " ops, seed " << seed << ", BBO compared after every op\n";
    std::cout << "levels at the end: " << all_bids.size() << " bid, " << all_asks.size() << " ask\n\n";

    bool pass_a = edge_a;
    bool pass_b = edge_b && same_b;
    bool pass_c = edge_c && same_c;
    std::cout << "Implementation A: " << (pass_a ? "PASS" : "FAIL") << "\n";
    std::cout << "Implementation B: " << (pass_b ? "PASS" : "FAIL") << "\n";
    std::cout << "Implementation C: " << (pass_c ? "PASS" : "FAIL") << "\n\n";

    if (pass_a && pass_b && pass_c) {
        std::cout << "All implementations produce identical results.\n";
        return 0;
    }
    std::cout << "Implementations do NOT agree.\n";
    return 1;
}

void usage() {
    std::cout << "usage: ./bench --impl A|B|C [--runs N] [--ops N]\n"
                 "       ./bench --verify [--ops N]\n";
}

int main(int argc, char** argv) {
    std::string impl;
    bool do_verify = false;
    int runs = 5;
    int n = 1000000;
    const uint32_t seed = 42;

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--verify") do_verify = true;
        else if (arg == "--impl" && i + 1 < argc) impl = argv[++i];
        else if (arg == "--runs" && i + 1 < argc) runs = std::stoi(argv[++i]);
        else if (arg == "--ops" && i + 1 < argc) n = std::stoi(argv[++i]);
        else {
            usage();
            return 1;
        }
    }

    if (do_verify) return verify(n, seed);

    if (runs < 1 || n < 1) {
        usage();
        return 1;
    }

    std::vector<Op> ops = make_workload(n, seed);
    if (impl == "A") bench<MapBook>("A (std::map)", ops, runs);
    else if (impl == "B") bench<HeapBook>("B (heap + lazy delete)", ops, runs);
    else if (impl == "C") bench<ArrayBook>("C (array)", ops, runs);
    else {
        usage();
        return 1;
    }
    return 0;
}

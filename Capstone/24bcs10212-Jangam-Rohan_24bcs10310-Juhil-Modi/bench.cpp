#include <algorithm>
#include <chrono>
#include <cstdio>
#include <random>
#include <string>
#include <vector>

#include "book_a.hpp"
#include "book_b.hpp"
#include "book_c.hpp"

using Clock = std::chrono::steady_clock;

const int NUM_OPS = 1000000;
const int FULL_DEPTH = 100000;  //more than the number of ticks, so get_bids returns every level

enum class OpType { Add, Cancel, Bbo };

struct Op {
    OpType type;
    uint64_t id;
    Side side;
    double price;
    long qty;
};

//all ops are made before timing, so every book replays the same list and the rng is not measured
//band = how many ticks from the middle a price can be, 5000 means the whole side
std::vector<Op> make_workload(int n, int add_pct, int cancel_pct, int band, unsigned seed) {
    std::mt19937 rng(seed);
    std::uniform_int_distribution<int> percent(0, 99);
    std::uniform_int_distribution<int> coin(0, 1);
    std::uniform_int_distribution<long> random_qty(1, 100);
    std::uniform_int_distribution<int> random_bid(MAX_BID_TICK - band + 1, MAX_BID_TICK);
    std::uniform_int_distribution<int> random_ask(MIN_ASK_TICK, MIN_ASK_TICK + band - 1);

    std::vector<Op> ops;
    std::vector<uint64_t> live_ids;  //ids still in the book, cancels pick from here
    uint64_t next_id = 0;

    for (int i = 0; i < n; i++) {
        Op op{};
        int roll = percent(rng);
        if (roll < add_pct) {
            op.type = OpType::Add;
        } else if (roll < add_pct + cancel_pct) {
            op.type = OpType::Cancel;
        } else {
            op.type = OpType::Bbo;
        }

        if (op.type == OpType::Cancel && live_ids.empty()) {
            op.type = OpType::Add;
        }

        if (op.type == OpType::Add) {
            op.id = next_id++;
            op.side = (coin(rng) == 0) ? Side::Buy : Side::Sell;
            int tick = (op.side == Side::Buy) ? random_bid(rng) : random_ask(rng);
            op.price = to_price(tick);
            op.qty = random_qty(rng);
            live_ids.push_back(op.id);
        } else if (op.type == OpType::Cancel) {
            std::uniform_int_distribution<size_t> pick(0, live_ids.size() - 1);
            size_t idx = pick(rng);
            op.id = live_ids[idx];
            live_ids[idx] = live_ids.back();  //swap with last and pop
            live_ids.pop_back();
        }
        ops.push_back(op);
    }
    return ops;
}

long long ns_since(Clock::time_point start) {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start).count();
}

//counts how many ops fell in each 10ns bucket, small enough that it doesn't disturb the cache
//anything slower than 10us goes in the last bucket
struct Histogram {
    std::vector<long long> counts = std::vector<long long>(1000, 0);
    long long total_ns = 0;
    long long n = 0;

    void add(long long ns) {
        total_ns += ns;
        n++;
        counts[std::min(ns / 10, 999LL)]++;
    }

    double average() const {
        return (double)total_ns / n;
    }

    //walk the buckets until 99% of ops are covered
    double p99() const {
        long long seen = 0;
        for (int b = 0; b < 1000; b++) {
            seen += counts[b];
            if (seen * 100 >= n * 99) return (b + 1) * 10.0;
        }
        return 10000;
    }
};

struct Result {
    double total_ms;
    double ns_add, ns_cancel, ns_bbo;
    double p99_add, p99_cancel, p99_bbo;
    long long checksum;
    BBO final_bbo;
};

//template so one loop can run any book, no virtual call inside the timing
template <typename Book>
Result run_once(const std::vector<Op>& ops) {
    Book book;
    Histogram add_times, cancel_times, bbo_times;
    long long checksum = 0;  //printed at the end so the compiler can't skip the work

    Clock::time_point start = Clock::now();
    for (size_t i = 0; i < ops.size(); i++) {
        const Op& op = ops[i];
        Clock::time_point t0 = Clock::now();

        if (op.type == OpType::Add) {
            book.add_order(op.id, op.side, op.price, op.qty);
            add_times.add(ns_since(t0));
        } else if (op.type == OpType::Cancel) {
            bool ok = book.cancel_order(op.id);
            cancel_times.add(ns_since(t0));
            checksum += ok;
        } else {
            BBO bbo = book.get_bbo();
            bbo_times.add(ns_since(t0));
            checksum += bbo.bid_qty + bbo.ask_qty;
        }

        if ((i + 1) % 100 == 0) {
            for (Level level : book.get_bids(10)) checksum += level.qty;
            for (Level level : book.get_asks(10)) checksum += level.qty;
        }
    }

    Result r{};
    r.total_ms = ns_since(start) / 1e6;
    r.ns_add = add_times.average();
    r.ns_cancel = cancel_times.average();
    r.ns_bbo = bbo_times.average();
    r.p99_add = add_times.p99();
    r.p99_cancel = cancel_times.p99();
    r.p99_bbo = bbo_times.p99();
    r.checksum = checksum;
    r.final_bbo = book.get_bbo();
    return r;
}

double median(std::vector<double> values) {
    std::sort(values.begin(), values.end());
    return values[values.size() / 2];
}

template <typename Book>
void run_bench(const char* name, int runs) {
    std::vector<Op> ops = make_workload(NUM_OPS, 70, 20, 5000, 42);
    printf("Implementation %s\n\n", name);

    std::vector<double> total, add, cancel, bbo, add99, cancel99, bbo99;
    Result r{};
    for (int run = 1; run <= runs; run++) {
        r = run_once<Book>(ops);
        total.push_back(r.total_ms);
        add.push_back(r.ns_add);
        cancel.push_back(r.ns_cancel);
        bbo.push_back(r.ns_bbo);
        add99.push_back(r.p99_add);
        cancel99.push_back(r.p99_cancel);
        bbo99.push_back(r.p99_bbo);
        printf("run %d: %.1f ms\n", run, r.total_ms);
    }

    printf("\nmedian of %d runs\n", runs);
    printf("Total time  : %.1f ms\n", median(total));
    printf("ns / add    : %.1f (p99 %.0f)\n", median(add), median(add99));
    printf("ns / cancel : %.1f (p99 %.0f)\n", median(cancel), median(cancel99));
    printf("ns / bbo    : %.1f (p99 %.0f)\n", median(bbo), median(bbo99));

    printf("\nFinal BBO:\n");
    printf("Bid: %.2f x %ld\n", r.final_bbo.bid_price, r.final_bbo.bid_qty);
    printf("Ask: %.2f x %ld\n", r.final_bbo.ask_price, r.final_bbo.ask_qty);
    printf("\nChecksum: %lld\n", r.checksum);
}

//the "required behavior" cases from the assignment
template <typename Book>
bool edge_cases_pass() {
    Book book;

    BBO empty = book.get_bbo();
    if (empty.has_bid || empty.has_ask) return false;
    if (book.cancel_order(12345)) return false;

    book.add_order(1, Side::Buy, 99.50, 10);
    book.add_order(2, Side::Buy, 99.40, 20);
    book.add_order(3, Side::Buy, 99.50, 5);
    book.add_order(4, Side::Sell, 100.20, 7);

    if (book.get_bids(10).size() != 2) return false;  //asked for 10 levels, only 2 exist
    if (book.get_asks(10).size() != 1) return false;

    BBO bbo = book.get_bbo();
    if (bbo.bid_price != 99.50 || bbo.bid_qty != 15) return false;
    if (bbo.ask_price != 100.20 || bbo.ask_qty != 7) return false;

    if (!book.cancel_order(1)) return false;
    if (book.cancel_order(1)) return false;  //already cancelled
    if (book.get_bbo().bid_qty != 5) return false;

    if (!book.cancel_order(3)) return false;
    if (book.get_bbo().bid_price != 99.40) return false;

    if (!book.cancel_order(4)) return false;
    if (book.get_bbo().has_ask) return false;
    return true;
}

//orders at the very edges of both price bands, and a level that empties and refills
template <typename Book>
bool band_edges_pass() {
    Book book;
    book.add_order(1, Side::Buy, 50.00, 1);
    book.add_order(2, Side::Sell, 150.00, 2);
    book.add_order(3, Side::Buy, 99.99, 3);
    book.add_order(4, Side::Sell, 100.01, 4);

    BBO bbo = book.get_bbo();
    if (bbo.bid_price != 99.99 || bbo.ask_price != 100.01) return false;

    book.cancel_order(3);
    book.cancel_order(4);
    bbo = book.get_bbo();
    if (bbo.bid_price != 50.00 || bbo.ask_price != 150.00) return false;

    book.add_order(5, Side::Buy, 99.99, 6);
    if (book.get_bbo().bid_price != 99.99 || book.get_bbo().bid_qty != 6) return false;

    book.cancel_order(1);
    book.cancel_order(2);
    book.cancel_order(5);
    bbo = book.get_bbo();
    if (bbo.has_bid || bbo.has_ask) return false;
    if (!book.get_bids(10).empty() || !book.get_asks(10).empty()) return false;
    return true;
}

//runs the ops through book a and the given book side by side, a is the reference
template <typename Book>
long long count_mismatches(const std::vector<Op>& ops) {
    MapOrderBook a;
    Book other;
    long long bad = 0;

    for (const Op& op : ops) {
        if (op.type == OpType::Add) {
            a.add_order(op.id, op.side, op.price, op.qty);
            other.add_order(op.id, op.side, op.price, op.qty);
        } else if (op.type == OpType::Cancel) {
            if (a.cancel_order(op.id) != other.cancel_order(op.id)) bad++;
        }

        if (!(a.get_bbo() == other.get_bbo())) bad++;
        if (a.get_bids(10) != other.get_bids(10)) bad++;
        if (a.get_asks(10) != other.get_asks(10)) bad++;
    }

    if (a.get_bids(FULL_DEPTH) != other.get_bids(FULL_DEPTH)) bad++;
    if (a.get_asks(FULL_DEPTH) != other.get_asks(FULL_DEPTH)) bad++;
    return bad;
}

const char* pass_fail(bool ok) {
    return ok ? "PASS" : "FAIL";
}

int run_verify() {
    printf("Running correctness test...\n\n");

    std::vector<Op> normal = make_workload(NUM_OPS, 70, 20, 5000, 42);
    //in the normal workload every tick fills up early, so the best level almost never empties
    //this one keeps only a few orders near the middle, so levels empty all the time
    std::vector<Op> tight = make_workload(200000, 45, 50, 10, 43);

    bool edge_a = edge_cases_pass<MapOrderBook>() && band_edges_pass<MapOrderBook>();
    bool edge_b = edge_cases_pass<VectorOrderBook>() && band_edges_pass<VectorOrderBook>();
    bool edge_c = edge_cases_pass<ArrayOrderBook>() && band_edges_pass<ArrayOrderBook>();
    printf("edge cases + band edges: A %s, B %s, C %s\n\n", pass_fail(edge_a), pass_fail(edge_b), pass_fail(edge_c));

    long long b_normal = count_mismatches<VectorOrderBook>(normal);
    long long c_normal = count_mismatches<ArrayOrderBook>(normal);
    long long b_tight = count_mismatches<VectorOrderBook>(tight);
    long long c_tight = count_mismatches<ArrayOrderBook>(tight);
    printf("mismatches vs A, checked after every op:\n");
    printf("  normal workload (%zu ops, 70%% add / 20%% cancel / 10%% bbo): B %lld, C %lld\n", normal.size(),
           b_normal, c_normal);
    printf("  tight workload  (%zu ops, 45%% add / 50%% cancel, 10 ticks wide): B %lld, C %lld\n\n", tight.size(),
           b_tight, c_tight);

    bool a_ok = edge_a;
    bool b_ok = edge_b && b_normal == 0 && b_tight == 0;
    bool c_ok = edge_c && c_normal == 0 && c_tight == 0;
    printf("Implementation A: %s\n", pass_fail(a_ok));
    printf("Implementation B: %s\n", pass_fail(b_ok));
    printf("Implementation C: %s\n\n", pass_fail(c_ok));

    if (a_ok && b_ok && c_ok) {
        printf("All implementations produce identical results.\n");
        return 0;
    }
    printf("Implementations do NOT agree.\n");
    return 1;
}

int main(int argc, char** argv) {
    std::string impl;
    int runs = 5;

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--verify") return run_verify();
        if (arg == "--impl" && i + 1 < argc) impl = argv[++i];
        if (arg == "--runs" && i + 1 < argc) runs = std::stoi(argv[++i]);
    }
    if (runs < 1) runs = 1;

    if (impl == "A") {
        run_bench<MapOrderBook>("A (std::map levels + unordered_map orders)", runs);
    } else if (impl == "B") {
        run_bench<VectorOrderBook>("B (sorted vector levels + std::map orders)", runs);
    } else if (impl == "C") {
        run_bench<ArrayOrderBook>("C (price indexed array + vector of orders by id)", runs);
    } else {
        printf("usage: bench --verify\n");
        printf("       bench --impl A|B|C [--runs N]\n");
        return 1;
    }
    return 0;
}

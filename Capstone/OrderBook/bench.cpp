#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <memory>
#include <random>
#include <string>
#include "book_array.hpp"
#include "book_heap.hpp"
#include "book_map.hpp"

using Clock = std::chrono::steady_clock;

enum class OpType { Add, Cancel, Bbo };

struct Op {
    OpType type;
    uint64_t id;
    Side side;
    double price;
    long qty;
};

// band = how many ticks on each side of the mid we use, 5000 = full range
std::vector<Op> make_workload(int n, uint32_t seed, int band = 5000) {
    std::mt19937 rng(seed);
    std::uniform_int_distribution<int> pct(0, 99), qty(1, 100), off(0, band - 1);
    std::vector<Op> ops;
    std::vector<uint64_t> live;
    ops.reserve(n);
    uint64_t next_id = 1;

    for (int i = 0; i < n; i++) {
        int r = pct(rng);
        if (r < 70 || (r < 90 && live.empty())) {
            Side s = pct(rng) < 50 ? Side::Buy : Side::Sell;
            int tick = s == Side::Buy ? BID_HI - off(rng) : ASK_LO + off(rng);
            ops.push_back({OpType::Add, next_id, s, to_price(tick), qty(rng)});
            live.push_back(next_id++);
        } else if (r < 90) {
            size_t k = std::uniform_int_distribution<size_t>(0, live.size() - 1)(rng);
            ops.push_back({OpType::Cancel, live[k], Side::Buy, 0, 0});
            live[k] = live.back();
            live.pop_back();
        } else {
            ops.push_back({OpType::Bbo, 0, Side::Buy, 0, 0});
        }
    }
    return ops;
}

uint64_t mix(uint64_t h, uint64_t v) { return (h ^ v) * 1099511628211ull; }

uint64_t hash_bbo(uint64_t h, const BBO& b) {
    if (b.has_bid) h = mix(mix(h, to_tick(b.bid_price)), b.bid_qty);
    if (b.has_ask) h = mix(mix(h, to_tick(b.ask_price)), b.ask_qty);
    return h;
}

uint64_t hash_levels(uint64_t h, const std::vector<Level>& lv) {
    for (auto& l : lv) h = mix(mix(h, to_tick(l.price)), l.qty);
    return mix(h, lv.size());
}

// ---------------- benchmark ----------------

struct Result {
    double total_ms, add_ns, cancel_ns, bbo_ns;
    uint64_t checksum;
    BBO final_bbo;
};

bool time_ops = true;

template <class Book>
Result run_once(const std::vector<Op>& ops) {
    auto book = std::make_unique<Book>();
    long long add_t = 0, cancel_t = 0, bbo_t = 0;
    long adds = 0, cancels = 0, bbos = 0;
    uint64_t h = 14695981039346656037ull;

    auto start = Clock::now();
    for (size_t i = 0; i < ops.size(); i++) {
        const Op& op = ops[i];
        auto t0 = time_ops ? Clock::now() : start;
        if (op.type == OpType::Add) {
            book->add_order(op.id, op.side, op.price, op.qty);
            if (time_ops) add_t += (Clock::now() - t0).count();
            adds++;
        } else if (op.type == OpType::Cancel) {
            bool ok = book->cancel_order(op.id);
            if (time_ops) cancel_t += (Clock::now() - t0).count();
            cancels++;
            h = mix(h, ok);
        } else {
            BBO b = book->get_bbo();
            if (time_ops) bbo_t += (Clock::now() - t0).count();
            bbos++;
            h = hash_bbo(h, b);
        }
        if ((i + 1) % 100 == 0) {
            h = hash_levels(h, book->get_bids(10));
            h = hash_levels(h, book->get_asks(10));
        }
    }
    double total = std::chrono::duration<double, std::milli>(Clock::now() - start).count();

    using ns = std::chrono::nanoseconds;
    auto to_ns = [](long long ticks) { return (double)std::chrono::duration_cast<ns>(Clock::duration(ticks)).count(); };
    return {total, to_ns(add_t) / adds, to_ns(cancel_t) / cancels, to_ns(bbo_t) / bbos, h, book->get_bbo()};
}

double median(std::vector<double> v) {
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

template <class Book>
void bench(const char* name, const std::vector<Op>& ops, int runs) {
    std::vector<double> total, add, cancel, bbo;
    Result r{};
    for (int i = 0; i < runs; i++) {
        r = run_once<Book>(ops);
        total.push_back(r.total_ms);
        add.push_back(r.add_ns);
        cancel.push_back(r.cancel_ns);
        bbo.push_back(r.bbo_ns);
        printf("  run %d: total %.2f ms | add %.1f ns | cancel %.1f ns | bbo %.1f ns\n", i + 1, r.total_ms, r.add_ns,
               r.cancel_ns, r.bbo_ns);
    }
    printf("Implementation %s (median of %d runs, %zu ops)\n", name, runs, ops.size());
    printf("  Total time   : %.2f ms\n", median(total));
    printf("  ns / add     : %.1f\n", median(add));
    printf("  ns / cancel  : %.1f\n", median(cancel));
    printf("  ns / BBO     : %.1f\n", median(bbo));
    printf("Final BBO:\n  Bid: %.2f x %ld\n  Ask: %.2f x %ld\n", r.final_bbo.bid_price, r.final_bbo.bid_qty,
           r.final_bbo.ask_price, r.final_bbo.ask_qty);
    printf("Checksum: %llu\n\n", (unsigned long long)r.checksum);
}

// ---------------- correctness ----------------

int failures;

void check(bool cond, const char* what) {
    if (!cond) {
        printf("    FAILED: %s\n", what);
        failures++;
    }
}

bool same(const BBO& a, const BBO& b) {
    if (a.has_bid != b.has_bid || a.has_ask != b.has_ask) return false;
    if (a.has_bid && (a.bid_price != b.bid_price || a.bid_qty != b.bid_qty)) return false;
    if (a.has_ask && (a.ask_price != b.ask_price || a.ask_qty != b.ask_qty)) return false;
    return true;
}

bool same(const std::vector<Level>& a, const std::vector<Level>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); i++)
        if (a[i].price != b[i].price || a[i].qty != b[i].qty) return false;
    return true;
}

bool level_is(const Level& l, double price, long qty) { return to_tick(l.price) == to_tick(price) && l.qty == qty; }

// hand written cases with known answers
template <class Book>
bool edge_tests() {
    int before = failures;
    auto bk = std::make_unique<Book>();
    Book& b = *bk;

    BBO e = b.get_bbo();
    check(!e.has_bid && !e.has_ask, "empty book has no bid/ask");
    check(b.get_bids().empty() && b.get_asks().empty(), "empty book has no levels");
    check(!b.cancel_order(42), "cancel unknown id returns false");

    b.add_order(1, Side::Buy, 99.50, 10);
    b.add_order(2, Side::Buy, 99.50, 5);
    b.add_order(3, Side::Buy, 99.40, 7);
    b.add_order(4, Side::Sell, 100.10, 3);
    b.add_order(5, Side::Sell, 100.20, 4);
    b.add_order(6, Side::Buy, 120.00, 9);
    b.add_order(7, Side::Sell, 99.00, 9);
    b.add_order(8, Side::Buy, 50.00, 1);
    b.add_order(9, Side::Sell, 150.00, 1);

    BBO x = b.get_bbo();
    check(x.has_bid && level_is({x.bid_price, x.bid_qty}, 99.50, 15), "best bid 99.50 x 15");
    check(x.has_ask && level_is({x.ask_price, x.ask_qty}, 100.10, 3), "best ask 100.10 x 3");

    auto bids = b.get_bids(10);
    check(bids.size() == 3, "insufficient depth returns 3 bid levels");
    check(bids.size() == 3 && level_is(bids[0], 99.50, 15) && level_is(bids[1], 99.40, 7) &&
              level_is(bids[2], 50.00, 1),
          "bid levels sorted best first");
    auto asks = b.get_asks(2);
    check(asks.size() == 2 && level_is(asks[0], 100.10, 3) && level_is(asks[1], 100.20, 4), "ask depth 2");
    check(b.get_bids(0).empty(), "depth 0 returns nothing");

    check(!b.cancel_order(6) && !b.cancel_order(7), "out of band orders were rejected");
    check(b.cancel_order(1), "cancel live order");
    check(!b.cancel_order(1), "double cancel returns false");
    x = b.get_bbo();
    check(level_is({x.bid_price, x.bid_qty}, 99.50, 5), "partial level after cancel");
    b.cancel_order(2);
    x = b.get_bbo();
    check(level_is({x.bid_price, x.bid_qty}, 99.40, 7), "best bid moves down when level empties");
    b.cancel_order(4);
    x = b.get_bbo();
    check(level_is({x.ask_price, x.ask_qty}, 100.20, 4), "best ask moves up when level empties");

    uint64_t big = 1ull << 40;
    b.add_order(big, Side::Buy, 99.99, 2);
    x = b.get_bbo();
    check(level_is({x.bid_price, x.bid_qty}, 99.99, 2), "huge order id works");
    check(b.cancel_order(big) && !b.cancel_order(big), "cancel huge order id");

    b.add_order(10, Side::Sell, 100.20, 6);
    b.add_order(10, Side::Sell, 100.30, 6);
    asks = b.get_asks();
    check(asks.size() == 2 && level_is(asks[0], 100.20, 10), "duplicate id is ignored");

    for (uint64_t id : {3, 5, 8, 9, 10}) b.cancel_order(id);
    x = b.get_bbo();
    check(!x.has_bid && !x.has_ask, "book empty again after cancelling everything");

    b.add_order(11, Side::Buy, 99.40, 3);
    x = b.get_bbo();
    check(level_is({x.bid_price, x.bid_qty}, 99.40, 3), "level reused after being emptied");
    return failures == before;
}

// run the same workload through all three in lockstep and compare after every op
long long cross_check(const std::vector<Op>& ops, long long& mismatches) {
    auto a = std::make_unique<MapBook>();
    auto b = std::make_unique<HeapBook>();
    auto c = std::make_unique<ArrayBook>();
    long long checks = 0;
    for (const Op& op : ops) {
        if (op.type == OpType::Add) {
            a->add_order(op.id, op.side, op.price, op.qty);
            b->add_order(op.id, op.side, op.price, op.qty);
            c->add_order(op.id, op.side, op.price, op.qty);
        } else if (op.type == OpType::Cancel) {
            bool ra = a->cancel_order(op.id), rb = b->cancel_order(op.id), rc = c->cancel_order(op.id);
            mismatches += !ra + (ra != rb) + (ra != rc);
        }
        BBO ba = a->get_bbo();
        auto bidsA = a->get_bids(10), asksA = a->get_asks(10);
        mismatches += !same(ba, b->get_bbo()) + !same(ba, c->get_bbo());
        mismatches += !same(bidsA, b->get_bids(10)) + !same(bidsA, c->get_bids(10));
        mismatches += !same(asksA, b->get_asks(10)) + !same(asksA, c->get_asks(10));
        checks++;
    }
    return checks;
}

int verify() {
    printf("Running correctness test...\n\n");
    printf("Edge case tests:\n");
    bool okA = edge_tests<MapBook>();
    printf("  Implementation A: %s\n", okA ? "PASS" : "FAIL");
    bool okB = edge_tests<HeapBook>();
    printf("  Implementation B: %s\n", okB ? "PASS" : "FAIL");
    bool okC = edge_tests<ArrayBook>();
    printf("  Implementation C: %s\n\n", okC ? "PASS" : "FAIL");

    struct Case { const char* name; int n; uint32_t seed; int band; };
    Case cases[] = {
        {"benchmark workload (1M ops, seed 42, full price range)", 1000000, 42, 5000},
        {"narrow book (200k ops, seed 7, 20 ticks per side)", 200000, 7, 20},
        {"tiny book (200k ops, seed 99, 2 ticks per side)", 200000, 99, 2},
    };
    bool agree = true;
    printf("Lockstep comparison (BBO + 10 bid levels + 10 ask levels after every op):\n");
    for (auto& cs : cases) {
        long long bad = 0;
        long long n = cross_check(make_workload(cs.n, cs.seed, cs.band), bad);
        printf("  %-56s %lld checks, %lld mismatches\n", cs.name, n, bad);
        agree = agree && bad == 0;
    }

    bool ok = okA && okB && okC && agree;
    printf("\n%s\n", ok ? "All implementations produce identical results." : "Implementations DISAGREE.");
    return ok ? 0 : 1;
}

int main(int argc, char** argv) {
    std::string impl = "ABC";
    int runs = 5;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--verify")) return verify();
        if (!strcmp(argv[i], "--impl") && i + 1 < argc) impl = argv[++i];
        else if (!strcmp(argv[i], "--runs") && i + 1 < argc) runs = std::max(1, atoi(argv[++i]));
        else if (!strcmp(argv[i], "--untimed")) time_ops = false;
        else {
            printf("usage: %s [--verify] [--impl A|B|C] [--runs N] [--untimed]\n", argv[0]);
            return 1;
        }
    }

    auto ops = make_workload(1000000, 42);
    for (char c : impl) {
        if (c == 'A') bench<MapBook>("A (std::map)", ops, runs);
        else if (c == 'B') bench<HeapBook>("B (heap + hash map)", ops, runs);
        else if (c == 'C') bench<ArrayBook>("C (array + bitmap)", ops, runs);
        else {
            printf("unknown implementation '%c'\n", c);
            return 1;
        }
    }
    return 0;
}

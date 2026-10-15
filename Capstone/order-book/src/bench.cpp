// Benchmark and correctness test for the three order book implementations.
//
//   ./bench --impl A|B|C [--runs N] [--ops N] [--seed S]
//   ./bench --impl A|B|C --untimed  (no per-op timers: total time only, for perf record)
//   ./bench --impl 0                (baseline: generate the workload only)
//   ./bench --verify     [--ops N] [--seed S]
//
// All implementations are driven through templates (same method names), so the
// workload code is shared and there is no virtual-call overhead.

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

#include "order_book_a.hpp"
#include "order_book_b.hpp"
#include "order_book_c.hpp"

namespace {

using Clock = std::chrono::steady_clock;

// ---------------------------------------------------------------- workload

enum class OpType : uint8_t { Add, Cancel, Bbo };

struct Op {
    OpType type;
    Side side;
    uint64_t id;
    double price;
    long qty;
};

struct WorkloadSpec {
    size_t ops = 1'000'000;
    uint32_t seed = 42;
    int add_pct = 70;     // add_pct + cancel_pct <= 100; the rest are BBO queries
    int cancel_pct = 20;
    uint32_t band_ticks = 5000;  // prices drawn from the `band_ticks` ticks nearest the mid
};

// Generates the whole operation stream up front so RNG cost is never timed and
// every implementation sees byte-identical input. Cancels always target a live
// order (tracked here with a swap-remove vector). If nothing is live, a cancel
// is replaced by an add.
//
// libc++ (macOS) and libstdc++ (Linux) implement std::uniform_int_distribution
// differently, so values are drawn straight from mt19937 with a modulo instead.
// The bias is < 1e-5 for these ranges and the workload is identical everywhere.
std::vector<Op> generate_workload(const WorkloadSpec& spec) {
    std::mt19937 rng(spec.seed);
    auto draw = [&rng](uint32_t n) { return static_cast<uint32_t>(rng() % n); };  // [0, n)

    std::vector<Op> ops;
    ops.reserve(spec.ops);
    std::vector<uint64_t> live;
    uint64_t next_id = 1;

    for (size_t i = 0; i < spec.ops; ++i) {
        const int r = static_cast<int>(draw(100));
        OpType type = r < spec.add_pct ? OpType::Add
                      : r < spec.add_pct + spec.cancel_pct ? OpType::Cancel
                                                           : OpType::Bbo;
        if (type == OpType::Cancel && live.empty()) type = OpType::Add;

        Op op{type, Side::Buy, 0, 0.0, 0};
        if (type == OpType::Add) {
            op.side = draw(2) ? Side::Buy : Side::Sell;
            const int offset = static_cast<int>(draw(spec.band_ticks));
            const int tick = op.side == Side::Buy ? book::kMaxBidTick - offset
                                                  : book::kMinAskTick + offset;
            op.price = book::to_price(tick);
            op.qty = 1 + static_cast<long>(draw(100));
            op.id = next_id++;
            live.push_back(op.id);
        } else if (type == OpType::Cancel) {
            const size_t k = draw(static_cast<uint32_t>(live.size()));
            op.id = live[k];
            live[k] = live.back();
            live.pop_back();
        }
        ops.push_back(op);
    }
    return ops;
}

constexpr size_t kDepthEvery = 100;  // get_bids(10) + get_asks(10) every 100 ops

// ---------------------------------------------------------------- benchmark

struct RunResult {
    double total_ms = 0;
    double ns_add = 0, ns_cancel = 0, ns_bbo = 0, ns_depth = 0;
    uint64_t checksum = 0;
    BBO final_bbo;
};

inline void mix(uint64_t& h, uint64_t v) { h = (h ^ v) * 0x100000001B3ull; }

inline void mix_levels(uint64_t& h, const std::vector<Level>& levels) {
    for (const Level& l : levels) {
        mix(h, static_cast<uint64_t>(std::llround(l.price * 100)));
        mix(h, static_cast<uint64_t>(l.qty));
    }
}

// Per-op timestamps; compiled out entirely in --untimed mode so that perf
// profiles show the order book rather than clock_gettime.
template <bool Timed>
inline Clock::time_point stamp() {
    if constexpr (Timed) return Clock::now();
    else return Clock::time_point{};
}

template <class Book, bool Timed>
RunResult run_once(const std::vector<Op>& ops) {
    auto book = std::make_unique<Book>();
    uint64_t t_add = 0, t_cancel = 0, t_bbo = 0, t_depth = 0;
    size_t n_add = 0, n_cancel = 0, n_bbo = 0, n_depth = 0;
    uint64_t h = 1469598103934665603ull;

    const auto start = Clock::now();
    for (size_t i = 0; i < ops.size(); ++i) {
        const Op& op = ops[i];
        const auto t0 = stamp<Timed>();
        switch (op.type) {
            case OpType::Add: {
                book->add_order(op.id, op.side, op.price, op.qty);
                const auto t1 = stamp<Timed>();
                t_add += (t1 - t0).count();
                ++n_add;
                break;
            }
            case OpType::Cancel: {
                const bool ok = book->cancel_order(op.id);
                const auto t1 = stamp<Timed>();
                t_cancel += (t1 - t0).count();
                ++n_cancel;
                mix(h, ok);
                break;
            }
            case OpType::Bbo: {
                const BBO b = book->get_bbo();
                const auto t1 = stamp<Timed>();
                t_bbo += (t1 - t0).count();
                ++n_bbo;
                mix(h, static_cast<uint64_t>(b.bid_qty) * 31 + static_cast<uint64_t>(b.ask_qty));
                break;
            }
        }
        if ((i + 1) % kDepthEvery == 0) {
            const auto t0d = stamp<Timed>();
            const auto bids = book->get_bids(10);
            const auto asks = book->get_asks(10);
            const auto t1d = stamp<Timed>();
            t_depth += (t1d - t0d).count();
            ++n_depth;
            mix_levels(h, bids);
            mix_levels(h, asks);
        }
    }
    const auto end = Clock::now();

    using ns = std::chrono::nanoseconds;
    auto per = [](uint64_t ticks, size_t n) {
        return n ? std::chrono::duration_cast<ns>(Clock::duration(ticks)).count() / double(n) : 0.0;
    };
    RunResult r;
    r.total_ms = std::chrono::duration<double, std::milli>(end - start).count();
    r.ns_add = per(t_add, n_add);
    r.ns_cancel = per(t_cancel, n_cancel);
    r.ns_bbo = per(t_bbo, n_bbo);
    r.ns_depth = per(t_depth, n_depth);
    r.final_bbo = book->get_bbo();
    r.checksum = h;
    return r;
}

// Cost of one back-to-back steady_clock::now() pair, to put per-op numbers in context.
double timer_overhead_ns() {
    constexpr int kIters = 100'000;
    uint64_t sum = 0;
    for (int i = 0; i < kIters; ++i) {
        const auto t0 = Clock::now();
        const auto t1 = Clock::now();
        sum += (t1 - t0).count();
    }
    return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::duration(sum)).count() /
           double(kIters);
}

double median(std::vector<double> v) {
    std::sort(v.begin(), v.end());
    const size_t n = v.size();
    return n % 2 ? v[n / 2] : (v[n / 2 - 1] + v[n / 2]) / 2.0;
}

void print_bbo(const BBO& b) {
    std::printf("Final BBO:  Bid: ");
    if (b.has_bid) std::printf("%.2f x %ld", b.bid_price, b.bid_qty); else std::printf("none");
    std::printf("  Ask: ");
    if (b.has_ask) std::printf("%.2f x %ld", b.ask_price, b.ask_qty); else std::printf("none");
    std::printf("\n");
}

template <class Book>
int benchmark(const char* name, const WorkloadSpec& spec, int runs, bool timed) {
    const auto ops = generate_workload(spec);
    size_t counts[3] = {0, 0, 0};
    for (const Op& op : ops) ++counts[static_cast<int>(op.type)];

    std::printf("Implementation %s | %zu ops (add %zu, cancel %zu, bbo %zu) + depth every %zu | seed %u\n",
                name, ops.size(), counts[0], counts[1], counts[2], kDepthEvery, spec.seed);
    if (timed)
        std::printf("steady_clock::now() pair overhead: %.1f ns (included in per-op numbers)\n\n",
                    timer_overhead_ns());
    else
        std::printf("--untimed: per-op timers compiled out, only total time is measured\n\n");
    std::printf("%-6s %12s %10s %10s %10s %12s %20s\n", "run", "total(ms)", "ns/add", "ns/cancel",
                "ns/bbo", "ns/depth", "checksum");

    std::vector<double> total, add, cancel, bbo, depth;
    RunResult last;
    for (int r = 0; r < runs; ++r) {
        last = timed ? run_once<Book, true>(ops) : run_once<Book, false>(ops);
        std::printf("%-6d %12.2f %10.1f %10.1f %10.1f %12.1f %20llu\n", r + 1, last.total_ms,
                    last.ns_add, last.ns_cancel, last.ns_bbo, last.ns_depth,
                    static_cast<unsigned long long>(last.checksum));
        total.push_back(last.total_ms);
        add.push_back(last.ns_add);
        cancel.push_back(last.ns_cancel);
        bbo.push_back(last.ns_bbo);
        depth.push_back(last.ns_depth);
    }
    std::printf("%-6s %12.2f %10.1f %10.1f %10.1f %12.1f\n\n", "median", median(total), median(add),
                median(cancel), median(bbo), median(depth));
    print_bbo(last.final_bbo);
    std::printf("Checksum: %llu\n", static_cast<unsigned long long>(last.checksum));
    return 0;
}

// ---------------------------------------------------------------- verification

// Deliberately simple reference model: aggregate qty per price in std::map,
// order id -> (side, tick, qty). Used as the oracle for all three books.
class ReferenceBook {
public:
    void add_order(uint64_t id, Side side, double price, long qty) {
        book::Tick t;
        if (!book::to_valid_tick(side, price, qty, t) || orders_.count(id)) return;
        orders_[id] = {side, t, qty};
        (side == Side::Buy ? bids_ : asks_)[t] += qty;
    }
    bool cancel_order(uint64_t id) {
        auto it = orders_.find(id);
        if (it == orders_.end()) return false;
        auto& ladder = it->second.side == Side::Buy ? bids_ : asks_;
        if ((ladder[it->second.tick] -= it->second.qty) == 0) ladder.erase(it->second.tick);
        orders_.erase(it);
        return true;
    }
    BBO get_bbo() const {
        BBO b;
        if (!bids_.empty()) b = {true, book::to_price(bids_.rbegin()->first), bids_.rbegin()->second};
        if (!asks_.empty()) {
            b.has_ask = true;
            b.ask_price = book::to_price(asks_.begin()->first);
            b.ask_qty = asks_.begin()->second;
        }
        return b;
    }
    std::vector<Level> get_bids(int depth = 10) const {
        std::vector<Level> out;
        for (auto it = bids_.rbegin(); it != bids_.rend() && (int)out.size() < depth; ++it)
            out.push_back({book::to_price(it->first), it->second});
        return out;
    }
    std::vector<Level> get_asks(int depth = 10) const {
        std::vector<Level> out;
        for (auto it = asks_.begin(); it != asks_.end() && (int)out.size() < depth; ++it)
            out.push_back({book::to_price(it->first), it->second});
        return out;
    }

private:
    struct O { Side side; book::Tick tick; long qty; };
    std::map<book::Tick, long> bids_, asks_;
    std::unordered_map<uint64_t, O> orders_;
};

struct Checker {
    explicit Checker(const char* n) : name(n) {}
    const char* name;
    bool ok = true;
    std::string first_error;

    void fail(const std::string& what) {
        if (ok) first_error = what;
        ok = false;
    }
};

template <class Book>
void compare_state(Checker& c, const Book& book, const ReferenceBook& ref, int depth, const std::string& where) {
    if (!c.ok) return;
    if (!(book.get_bbo() == ref.get_bbo())) c.fail(where + ": BBO mismatch");
    else if (book.get_bids(depth) != ref.get_bids(depth)) c.fail(where + ": bid levels mismatch");
    else if (book.get_asks(depth) != ref.get_asks(depth)) c.fail(where + ": ask levels mismatch");
}

// Hand-written edge cases from the spec, run against one implementation.
template <class Book>
void edge_cases(Checker& c) {
    auto expect = [&](bool cond, const char* what) { if (!cond) c.fail(std::string("edge case: ") + what); };
    Book b;
    BBO e = b.get_bbo();
    expect(!e.has_bid && !e.has_ask, "empty book has no bid/ask");
    expect(b.get_bids(10).empty() && b.get_asks(10).empty(), "empty book has no levels");
    expect(!b.cancel_order(12345), "cancel unknown id returns false");

    b.add_order(1, Side::Buy, 99.50, 10);
    b.add_order(2, Side::Buy, 99.50, 15);   // same price -> level qty 25
    b.add_order(3, Side::Buy, 99.00, 5);
    b.add_order(4, Side::Buy, 98.75, 7);
    b.add_order(5, Side::Sell, 100.01, 20);
    b.add_order(6, Side::Sell, 101.00, 30);

    auto bids = b.get_bids(10);
    expect(bids.size() == 3, "insufficient depth returns available levels");
    expect(bids.size() == 3 && bids[0] == Level{99.50, 25} && bids[1] == Level{99.00, 5} &&
               bids[2] == Level{98.75, 7}, "bid levels sorted best-first with summed qty");
    auto asks = b.get_asks(1);
    expect(asks.size() == 1 && asks[0] == Level{100.01, 20}, "get_asks(1) returns only the best");
    BBO bbo = b.get_bbo();
    expect(bbo.has_bid && bbo.bid_price == 99.50 && bbo.bid_qty == 25 && bbo.has_ask &&
               bbo.ask_price == 100.01 && bbo.ask_qty == 20, "BBO after adds");

    expect(b.cancel_order(1), "cancel live order returns true");
    expect(!b.cancel_order(1), "cancel already-cancelled order returns false");
    expect(b.get_bbo().bid_qty == 15, "partial level cancel reduces qty");
    expect(b.cancel_order(2), "cancel last order at best bid");
    expect(b.get_bbo().bid_price == 99.00, "best bid moves to next level");
    expect(b.cancel_order(5), "cancel best ask");
    expect(b.get_bbo().ask_price == 101.00, "best ask moves to next level");

    b.add_order(7, Side::Buy, 99.555, 10);   // off-tick
    b.add_order(8, Side::Buy, 100.50, 10);   // bid above the bid band
    b.add_order(9, Side::Sell, 99.00, 10);   // ask below the ask band
    b.add_order(10, Side::Buy, 99.00, 0);    // zero qty
    b.add_order(3, Side::Buy, 98.00, 10);    // duplicate id
    expect(b.get_bids(10).size() == 2 && b.get_asks(10).size() == 1, "invalid orders are rejected");
    expect(!b.cancel_order(7) && !b.cancel_order(8), "rejected orders cannot be cancelled");

    expect(b.cancel_order(3) && b.cancel_order(4) && b.cancel_order(6), "drain book");
    e = b.get_bbo();
    expect(!e.has_bid && !e.has_ask, "drained book is empty again");
    b.add_order(11, Side::Sell, 150.00, 1);
    b.add_order(12, Side::Buy, 50.00, 1);
    bbo = b.get_bbo();
    expect(bbo.bid_price == 50.00 && bbo.ask_price == 150.00, "band edges accepted after drain");
}

// Streams one workload through the reference and all three books, comparing
// cancel results, BBO, and top-10 levels after every op, and full depth periodically.
void cross_check(const WorkloadSpec& spec, Checker& a, Checker& b, Checker& c) {
    const auto ops = generate_workload(spec);
    ReferenceBook ref;
    auto A = std::make_unique<OrderBookA>();
    auto B = std::make_unique<OrderBookB>();
    auto C = std::make_unique<OrderBookC>();

    for (size_t i = 0; i < ops.size(); ++i) {
        const Op& op = ops[i];
        if (op.type == OpType::Add) {
            ref.add_order(op.id, op.side, op.price, op.qty);
            A->add_order(op.id, op.side, op.price, op.qty);
            B->add_order(op.id, op.side, op.price, op.qty);
            C->add_order(op.id, op.side, op.price, op.qty);
        } else if (op.type == OpType::Cancel) {
            const bool expected = ref.cancel_order(op.id);
            if (A->cancel_order(op.id) != expected) a.fail("cancel result mismatch");
            if (B->cancel_order(op.id) != expected) b.fail("cancel result mismatch");
            if (C->cancel_order(op.id) != expected) c.fail("cancel result mismatch");
            // Re-cancelling must now fail.
            if (A->cancel_order(op.id)) a.fail("double cancel returned true");
            if (B->cancel_order(op.id)) b.fail("double cancel returned true");
            if (C->cancel_order(op.id)) c.fail("double cancel returned true");
        }
        const std::string where = "op " + std::to_string(i);
        const int depth = (i % 1000 == 999) ? 1 << 20 : 10;  // full book every 1000 ops
        compare_state(a, *A, ref, depth, where);
        compare_state(b, *B, ref, depth, where);
        compare_state(c, *C, ref, depth, where);
        if (!a.ok && !b.ok && !c.ok) return;
    }
    compare_state(a, *A, ref, 1 << 20, "final");
    compare_state(b, *B, ref, 1 << 20, "final");
    compare_state(c, *C, ref, 1 << 20, "final");
}

int verify(const WorkloadSpec& base) {
    std::printf("Running correctness test...\n");
    Checker a{"A"}, b{"B"}, c{"C"};

    edge_cases<OrderBookA>(a);
    edge_cases<OrderBookB>(b);
    edge_cases<OrderBookC>(c);
    std::printf("  edge cases (empty book, unknown/double cancel, depth, invalid input): done\n");

    struct Scenario { const char* label; WorkloadSpec spec; };
    std::vector<Scenario> scenarios;
    scenarios.push_back({"benchmark mix 70/20/10, full price band", base});
    WorkloadSpec narrow = base;
    narrow.band_ticks = 20;      // few levels: best levels empty and refill constantly
    narrow.seed = base.seed + 1;
    scenarios.push_back({"narrow band (20 ticks/side)", narrow});
    WorkloadSpec drain = base;
    drain.add_pct = 50;
    drain.cancel_pct = 45;       // cancel-heavy: book repeatedly drains to empty
    drain.band_ticks = 50;
    drain.seed = base.seed + 2;
    drain.ops = std::min<size_t>(base.ops, 200'000);
    scenarios.push_back({"cancel-heavy 50/45/5, 50 ticks/side", drain});

    for (const auto& s : scenarios) {
        cross_check(s.spec, a, b, c);
        std::printf("  workload: %-40s %zu ops, seed %u: done\n", s.label, s.spec.ops, s.spec.seed);
    }

    std::printf("\n");
    for (const Checker* ch : {&a, &b, &c}) {
        std::printf("Implementation %s: %s", ch->name, ch->ok ? "PASS" : "FAIL");
        if (!ch->ok) std::printf("  (%s)", ch->first_error.c_str());
        std::printf("\n");
    }
    const bool all = a.ok && b.ok && c.ok;
    std::printf("\n%s\n", all ? "All implementations produce identical results."
                              : "Implementations DISAGREE.");
    return all ? 0 : 1;
}

[[noreturn]] void usage() {
    std::fprintf(stderr,
                 "usage: bench --impl A|B|C|0 [--runs N] [--ops N] [--seed S] [--untimed]   (0 = baseline, no book)\n"
                 "       bench --verify [--ops N] [--seed S]\n");
    std::exit(2);
}

}  // namespace

int main(int argc, char** argv) {
    WorkloadSpec spec;
    char impl = 0;
    int runs = 5;
    bool timed = true;
    bool do_verify = false;

    for (int i = 1; i < argc; ++i) {
        auto next = [&]() -> const char* { if (i + 1 >= argc) usage(); return argv[++i]; };
        if (!std::strcmp(argv[i], "--impl")) impl = next()[0];
        else if (!std::strcmp(argv[i], "--runs")) runs = std::atoi(next());
        else if (!std::strcmp(argv[i], "--ops")) spec.ops = std::strtoull(next(), nullptr, 10);
        else if (!std::strcmp(argv[i], "--seed")) spec.seed = static_cast<uint32_t>(std::strtoul(next(), nullptr, 10));
        else if (!std::strcmp(argv[i], "--verify")) do_verify = true;
        else if (!std::strcmp(argv[i], "--untimed")) timed = false;
        else usage();
    }
    if (runs < 1) usage();

    if (do_verify) return verify(spec);
    switch (impl) {
        case '0': {  // baseline: workload generation only, to subtract from whole-process perf counters
            const auto ops = generate_workload(spec);
            uint64_t h = 0;
            for (const Op& op : ops) mix(h, op.id + static_cast<uint64_t>(op.qty));
            std::printf("Baseline (workload generation only): %zu ops, checksum %llu\n", ops.size(),
                        static_cast<unsigned long long>(h));
            return 0;
        }
        case 'A': case 'a': return benchmark<OrderBookA>("A (std::map + std::list + unordered_map)", spec, runs, timed);
        case 'B': case 'b': return benchmark<OrderBookB>("B (priority_queue lazy-delete + unordered_maps)", spec, runs, timed);
        case 'C': case 'c': return benchmark<OrderBookC>("C (price-indexed array + bitmap + pool + open addressing)", spec, runs, timed);
        default: usage();
    }
}

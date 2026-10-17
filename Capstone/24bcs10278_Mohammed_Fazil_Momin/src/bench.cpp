#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <vector>

#include "order_book_first.hpp"
#include "order_book_second.hpp"
#include "order_book_third.hpp"

enum class OpType { Add, Cancel, Bbo };

struct Op {
    OpType type;
    std::uint64_t id;
    Side side;
    double price;
    long qty;
};

struct Book {
    virtual ~Book() {}
    virtual void add_order(std::uint64_t id, Side side, double price, long qty) = 0;
    virtual bool cancel_order(std::uint64_t id) = 0;
    virtual BBO get_bbo() const = 0;
    virtual std::vector<Level> get_bids(int depth) const = 0;
    virtual std::vector<Level> get_asks(int depth) const = 0;
};

struct BookA : Book {
    OrderBookA b;
    void add_order(std::uint64_t id, Side side, double price, long qty) override { b.add_order(id, side, price, qty); }
    bool cancel_order(std::uint64_t id) override { return b.cancel_order(id); }
    BBO get_bbo() const override { return b.get_bbo(); }
    std::vector<Level> get_bids(int depth) const override { return b.get_bids(depth); }
    std::vector<Level> get_asks(int depth) const override { return b.get_asks(depth); }
};

struct BookB : Book {
    OrderBookB b;
    void add_order(std::uint64_t id, Side side, double price, long qty) override { b.add_order(id, side, price, qty); }
    bool cancel_order(std::uint64_t id) override { return b.cancel_order(id); }
    BBO get_bbo() const override { return b.get_bbo(); }
    std::vector<Level> get_bids(int depth) const override { return b.get_bids(depth); }
    std::vector<Level> get_asks(int depth) const override { return b.get_asks(depth); }
};

struct BookC : Book {
    OrderBookC b;
    void add_order(std::uint64_t id, Side side, double price, long qty) override { b.add_order(id, side, price, qty); }
    bool cancel_order(std::uint64_t id) override { return b.cancel_order(id); }
    BBO get_bbo() const override { return b.get_bbo(); }
    std::vector<Level> get_bids(int depth) const override { return b.get_bids(depth); }
    std::vector<Level> get_asks(int depth) const override { return b.get_asks(depth); }
};

static std::vector<Op> make_workload(std::size_t count, unsigned seed) {
    std::mt19937 rng(seed);
    std::uniform_int_distribution<int> pick(0, 99);
    std::uniform_int_distribution<int> bid_tick(5000, 9999);
    std::uniform_int_distribution<int> ask_tick(10001, 15000);
    std::uniform_int_distribution<long> qty(1, 100);

    std::vector<Op> ops;
    ops.reserve(count);
    std::vector<std::uint64_t> live;
    std::uint64_t next_id = 1;

    for (std::size_t i = 0; i < count; ++i) {
        int r = pick(rng);
        if (r < 20 && !live.empty()) {
            std::uniform_int_distribution<std::size_t> idx(0, live.size() - 1);
            std::size_t k = idx(rng);
            ops.push_back({OpType::Cancel, live[k], Side::Buy, 0.0, 0});
            live[k] = live.back();
            live.pop_back();
        } else if (r < 90) {
            Side side = pick(rng) < 50 ? Side::Buy : Side::Sell;
            int tick = side == Side::Buy ? bid_tick(rng) : ask_tick(rng);
            ops.push_back({OpType::Add, next_id, side, tick / 100.0, qty(rng)});
            live.push_back(next_id++);
        } else {
            ops.push_back({OpType::Bbo, 0, Side::Buy, 0.0, 0});
        }
    }
    return ops;
}

static std::uint64_t mix(std::uint64_t h, std::uint64_t v) {
    return (h ^ v) * 1099511628211ULL;
}

static std::uint64_t fold_bbo(std::uint64_t h, const BBO& b) {
    h = mix(h, b.has_bid ? static_cast<std::uint64_t>(b.bid_price * 100.0 + 0.5) : 0);
    h = mix(h, static_cast<std::uint64_t>(b.bid_qty));
    h = mix(h, b.has_ask ? static_cast<std::uint64_t>(b.ask_price * 100.0 + 0.5) : 0);
    h = mix(h, static_cast<std::uint64_t>(b.ask_qty));
    return h;
}

static std::uint64_t fold_levels(std::uint64_t h, const std::vector<Level>& v) {
    for (const Level& l : v) {
        h = mix(h, static_cast<std::uint64_t>(l.price * 100.0 + 0.5));
        h = mix(h, static_cast<std::uint64_t>(l.qty));
    }
    return h;
}

struct RunResult {
    double total_ms;
    double ns_add;
    double ns_cancel;
    double ns_bbo;
    BBO final_bbo;
    std::uint64_t checksum;
};

static RunResult run_once(Book& book, const std::vector<Op>& ops) {
    using clock = std::chrono::steady_clock;
    long long add_ns = 0, cancel_ns = 0, bbo_ns = 0;
    long adds = 0, cancels = 0, bbos = 0;
    std::uint64_t checksum = 1469598103934665603ULL;

    auto start = clock::now();
    for (std::size_t i = 0; i < ops.size(); ++i) {
        const Op& op = ops[i];
        if (op.type == OpType::Add) {
            auto t0 = clock::now();
            book.add_order(op.id, op.side, op.price, op.qty);
            add_ns += (clock::now() - t0).count();
            ++adds;
        } else if (op.type == OpType::Cancel) {
            auto t0 = clock::now();
            bool ok = book.cancel_order(op.id);
            cancel_ns += (clock::now() - t0).count();
            ++cancels;
            checksum = mix(checksum, ok);
        } else {
            auto t0 = clock::now();
            BBO b = book.get_bbo();
            bbo_ns += (clock::now() - t0).count();
            ++bbos;
            checksum = fold_bbo(checksum, b);
        }
        if ((i + 1) % 100 == 0) {
            checksum = fold_levels(checksum, book.get_bids(10));
            checksum = fold_levels(checksum, book.get_asks(10));
        }
    }
    auto end = clock::now();

    RunResult r;
    r.total_ms = std::chrono::duration<double, std::milli>(end - start).count();
    r.ns_add = adds ? double(add_ns) / adds : 0;
    r.ns_cancel = cancels ? double(cancel_ns) / cancels : 0;
    r.ns_bbo = bbos ? double(bbo_ns) / bbos : 0;
    r.final_bbo = book.get_bbo();
    r.checksum = fold_bbo(checksum, r.final_bbo);
    return r;
}

static double median(std::vector<double> v) {
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

static Book* make_book(char impl) {
    if (impl == 'A') return new BookA;
    if (impl == 'B') return new BookB;
    return new BookC;
}

static int run_bench(char impl, const std::vector<Op>& ops, int runs) {
    std::vector<double> total, add, cancel, bbo;
    RunResult last{};
    for (int i = 0; i < runs; ++i) {
        Book* book = make_book(impl);
        last = run_once(*book, ops);
        delete book;
        total.push_back(last.total_ms);
        add.push_back(last.ns_add);
        cancel.push_back(last.ns_cancel);
        bbo.push_back(last.ns_bbo);
        std::printf("run %d: total %.2f ms, add %.1f ns, cancel %.1f ns, bbo %.1f ns\n",
                    i + 1, last.total_ms, last.ns_add, last.ns_cancel, last.ns_bbo);
    }
    std::printf("\nImplementation %c, median of %d runs over %zu operations\n", impl, runs, ops.size());
    std::printf("Total time (ms): %.2f\n", median(total));
    std::printf("ns / add:        %.1f\n", median(add));
    std::printf("ns / cancel:     %.1f\n", median(cancel));
    std::printf("ns / BBO:        %.1f\n", median(bbo));
    std::printf("\nFinal BBO:\n");
    if (last.final_bbo.has_bid) std::printf("Bid: %.2f x %ld\n", last.final_bbo.bid_price, last.final_bbo.bid_qty);
    else                        std::printf("Bid: none\n");
    if (last.final_bbo.has_ask) std::printf("Ask: %.2f x %ld\n", last.final_bbo.ask_price, last.final_bbo.ask_qty);
    else                        std::printf("Ask: none\n");
    std::printf("\nChecksum: %llu\n", static_cast<unsigned long long>(last.checksum));
    return 0;
}

static bool same_bbo(const BBO& x, const BBO& y) {
    return x.has_bid == y.has_bid && x.has_ask == y.has_ask &&
           (!x.has_bid || (x.bid_price == y.bid_price && x.bid_qty == y.bid_qty)) &&
           (!x.has_ask || (x.ask_price == y.ask_price && x.ask_qty == y.ask_qty));
}

static bool same_levels(const std::vector<Level>& x, const std::vector<Level>& y) {
    if (x.size() != y.size()) return false;
    for (std::size_t i = 0; i < x.size(); ++i) {
        if (x[i].price != y[i].price || x[i].qty != y[i].qty) return false;
    }
    return true;
}

static int run_verify(const std::vector<Op>& ops) {
    std::printf("Running correctness test over %zu operations...\n\n", ops.size());
    BookA a;
    BookB b;
    BookC c;
    bool ab = true, ac = true, bc = true;
    std::size_t first_mismatch = 0;

    for (std::size_t i = 0; i < ops.size(); ++i) {
        const Op& op = ops[i];
        if (op.type == OpType::Add) {
            a.add_order(op.id, op.side, op.price, op.qty);
            b.add_order(op.id, op.side, op.price, op.qty);
            c.add_order(op.id, op.side, op.price, op.qty);
        } else if (op.type == OpType::Cancel) {
            bool ra = a.cancel_order(op.id);
            bool rb = b.cancel_order(op.id);
            bool rc = c.cancel_order(op.id);
            if (ra != rb) ab = false;
            if (ra != rc) ac = false;
            if (rb != rc) bc = false;
        }
        BBO ba = a.get_bbo(), bb = b.get_bbo(), bc_ = c.get_bbo();
        if (!same_bbo(ba, bb)) ab = false;
        if (!same_bbo(ba, bc_)) ac = false;
        if (!same_bbo(bb, bc_)) bc = false;

        if ((i + 1) % 100 == 0 || i + 1 == ops.size()) {
            int depth = (i + 1 == ops.size()) ? 10000 : 10;
            std::vector<Level> la = a.get_bids(depth), lb = b.get_bids(depth), lc = c.get_bids(depth);
            std::vector<Level> ka = a.get_asks(depth), kb = b.get_asks(depth), kc = c.get_asks(depth);
            if (!same_levels(la, lb) || !same_levels(ka, kb)) ab = false;
            if (!same_levels(la, lc) || !same_levels(ka, kc)) ac = false;
            if (!same_levels(lb, lc) || !same_levels(kb, kc)) bc = false;
        }
        if (first_mismatch == 0 && !(ab && ac && bc)) first_mismatch = i + 1;
    }

    bool pass_a = ab && ac;
    bool pass_b = ab && bc;
    bool pass_c = ac && bc;
    std::printf("Implementation A: %s\n", pass_a ? "PASS" : "FAIL");
    std::printf("Implementation B: %s\n", pass_b ? "PASS" : "FAIL");
    std::printf("Implementation C: %s\n", pass_c ? "PASS" : "FAIL");
    if (pass_a && pass_b && pass_c) {
        std::printf("\nAll implementations produce identical results.\n");
        return 0;
    }
    std::printf("\nFirst mismatch at operation %zu.\n", first_mismatch);
    return 1;
}

int main(int argc, char** argv) {
    char impl = 0;
    bool verify = false;
    int runs = 5;
    std::size_t count = 1000000;
    unsigned seed = 42;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--impl") == 0 && i + 1 < argc) impl = argv[++i][0];
        else if (std::strcmp(argv[i], "--verify") == 0) verify = true;
        else if (std::strcmp(argv[i], "--runs") == 0 && i + 1 < argc) runs = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--ops") == 0 && i + 1 < argc) count = std::strtoull(argv[++i], nullptr, 10);
        else if (std::strcmp(argv[i], "--seed") == 0 && i + 1 < argc) seed = std::atoi(argv[++i]);
    }

    if (!verify && impl != 'A' && impl != 'B' && impl != 'C') {
        std::printf("usage: bench --impl A|B|C [--runs N] [--ops N] [--seed N]\n");
        std::printf("       bench --verify [--ops N] [--seed N]\n");
        return 2;
    }

    std::vector<Op> ops = make_workload(count, seed);
    if (verify) return run_verify(ops);
    return run_bench(impl, ops, runs);
}

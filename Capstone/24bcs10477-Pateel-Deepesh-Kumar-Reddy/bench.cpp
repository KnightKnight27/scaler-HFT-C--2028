#include "common.hpp"
#include "orderbook_a.hpp"
#include "orderbook_b.hpp"
#include "orderbook_c.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <random>
#include <string>
#include <vector>

struct Op {
    enum Type { Add, Cancel, Bbo } type;
    uint64_t id;
    Side side;
    int price;
    long qty;
};

static std::vector<Op> build_workload(int n_ops, uint32_t seed) {
    std::mt19937 rng(seed);
    std::uniform_int_distribution<int> pick(0, 99);
    std::uniform_int_distribution<int> side_pick(0, 1);
    std::uniform_int_distribution<int> qty_pick(1, 100);
    std::uniform_int_distribution<int> bid_px(BID_BASE, BID_MAX);
    std::uniform_int_distribution<int> ask_px(ASK_BASE, ASK_MAX);

    std::vector<Op> ops;
    ops.reserve(static_cast<size_t>(n_ops));

    std::vector<uint64_t> live;
    live.reserve(static_cast<size_t>(n_ops));
    uint64_t next_id = 1;

    auto make_add = [&]() {
        Side s = side_pick(rng) == 0 ? Side::Buy : Side::Sell;
        Op op{};
        op.type = Op::Add;
        op.id = next_id++;
        op.side = s;
        op.price = s == Side::Buy ? bid_px(rng) : ask_px(rng);
        op.qty = qty_pick(rng);
        live.push_back(op.id);
        return op;
    };

    for (int i = 0; i < n_ops; ++i) {
        int r = pick(rng);
        if (r < 70) {
            ops.push_back(make_add());
        } else if (r < 90) {
            if (live.empty()) {
                ops.push_back(make_add());
            } else {
                std::uniform_int_distribution<size_t> live_at(0, live.size() - 1);
                size_t idx = live_at(rng);
                Op op{};
                op.type = Op::Cancel;
                op.id = live[idx];
                live[idx] = live.back();
                live.pop_back();
                ops.push_back(op);
            }
        } else {
            Op op{};
            op.type = Op::Bbo;
            op.id = 0;
            op.side = Side::Buy;
            op.price = 0;
            op.qty = 0;
            ops.push_back(op);
        }
    }
    return ops;
}

using Clock = std::chrono::steady_clock;

struct RunResult {
    double total_ms;
    double ns_add;
    double ns_cancel;
    double ns_bbo;
    uint64_t checksum;
    BBO final_bbo;
};

static double median(std::vector<double> v) {
    std::sort(v.begin(), v.end());
    size_t n = v.size();
    return n % 2 ? v[n / 2] : 0.5 * (v[n / 2 - 1] + v[n / 2]);
}

template <typename Book>
static RunResult run(const std::vector<Op>& ops, int runs) {
    std::vector<double> totals, adds, cancels, bbos;
    RunResult last{};

    for (int r = 0; r < runs; ++r) {
        Book book;
        uint64_t checksum = 0;
        uint64_t ns_add = 0, ns_cancel = 0, ns_bbo = 0;
        uint64_t cnt_add = 0, cnt_cancel = 0, cnt_bbo = 0;

        auto t0 = Clock::now();
        for (size_t i = 0; i < ops.size(); ++i) {
            const Op& op = ops[i];
            auto s = Clock::now();
            switch (op.type) {
                case Op::Add: {
                    book.add_order(op.id, op.side, op.price, op.qty);
                    auto e = Clock::now();
                    ns_add += static_cast<uint64_t>(
                        std::chrono::duration_cast<std::chrono::nanoseconds>(e - s).count());
                    ++cnt_add;
                    break;
                }
                case Op::Cancel: {
                    checksum += book.cancel_order(op.id) ? 1 : 0;
                    auto e = Clock::now();
                    ns_cancel += static_cast<uint64_t>(
                        std::chrono::duration_cast<std::chrono::nanoseconds>(e - s).count());
                    ++cnt_cancel;
                    break;
                }
                case Op::Bbo: {
                    BBO b = book.get_bbo();
                    checksum += static_cast<uint64_t>(b.bid_price + b.bid_qty + b.ask_price + b.ask_qty);
                    auto e = Clock::now();
                    ns_bbo += static_cast<uint64_t>(
                        std::chrono::duration_cast<std::chrono::nanoseconds>(e - s).count());
                    ++cnt_bbo;
                    break;
                }
            }

            if (i % 100 == 99) {
                auto bids = book.get_bids(10);
                auto asks = book.get_asks(10);
                checksum += bids.size() + asks.size();
                if (!bids.empty()) checksum += bids.front().price + bids.front().qty;
                if (!asks.empty()) checksum += asks.front().price + asks.front().qty;
            }
        }
        auto t1 = Clock::now();

        totals.push_back(std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count() / 1e6);
        adds.push_back(cnt_add ? static_cast<double>(ns_add) / cnt_add : 0.0);
        cancels.push_back(cnt_cancel ? static_cast<double>(ns_cancel) / cnt_cancel : 0.0);
        bbos.push_back(cnt_bbo ? static_cast<double>(ns_bbo) / cnt_bbo : 0.0);

        last.checksum = checksum;
        last.final_bbo = book.get_bbo();
    }

    RunResult out{};
    out.total_ms = median(totals);
    out.ns_add = median(adds);
    out.ns_cancel = median(cancels);
    out.ns_bbo = median(bbos);
    out.checksum = last.checksum;
    out.final_bbo = last.final_bbo;
    return out;
}

static void print_result(const char* name, const RunResult& r) {
    printf("Implementation: %s\n", name);
    printf("Total time (ms):   %.2f\n", r.total_ms);
    printf("ns / add:          %.1f\n", r.ns_add);
    printf("ns / cancel:       %.1f\n", r.ns_cancel);
    printf("ns / BBO:          %.1f\n", r.ns_bbo);
    printf("Final BBO:\n");
    if (r.final_bbo.has_bid)
        printf("  Bid: %.2f x %ld\n", to_price(r.final_bbo.bid_price), r.final_bbo.bid_qty);
    else
        printf("  Bid: (none)\n");
    if (r.final_bbo.has_ask)
        printf("  Ask: %.2f x %ld\n", to_price(r.final_bbo.ask_price), r.final_bbo.ask_qty);
    else
        printf("  Ask: (none)\n");
    printf("Checksum: %llu\n\n", static_cast<unsigned long long>(r.checksum));
}

static bool same_bbo(const BBO& a, const BBO& b) {
    return a.has_bid == b.has_bid && a.has_ask == b.has_ask &&
           (!a.has_bid || (a.bid_price == b.bid_price && a.bid_qty == b.bid_qty)) &&
           (!a.has_ask || (a.ask_price == b.ask_price && a.ask_qty == b.ask_qty));
}

static bool same_levels(const std::vector<Level>& a, const std::vector<Level>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (a[i].price != b[i].price || a[i].qty != b[i].qty) return false;
    return true;
}

template <typename Book>
static bool compare_against_a(const std::vector<Op>& ops, size_t& fail_index) {
    OrderBookA ref;
    Book book;
    for (size_t i = 0; i < ops.size(); ++i) {
        const Op& op = ops[i];
        bool rc_ref = false, rc_book = false;
        switch (op.type) {
            case Op::Add:
                ref.add_order(op.id, op.side, op.price, op.qty);
                book.add_order(op.id, op.side, op.price, op.qty);
                break;
            case Op::Cancel:
                rc_ref = ref.cancel_order(op.id);
                rc_book = book.cancel_order(op.id);
                break;
            case Op::Bbo:
                break;
        }
        if (rc_ref != rc_book) { fail_index = i; return false; }
        if (!same_bbo(ref.get_bbo(), book.get_bbo())) { fail_index = i; return false; }
        if (!same_levels(ref.get_bids(10), book.get_bids(10))) { fail_index = i; return false; }
        if (!same_levels(ref.get_asks(10), book.get_asks(10))) { fail_index = i; return false; }
    }
    return true;
}

template <typename Book>
static bool edge_cases(const char* name) {
    auto fail = [&](const char* what) {
        printf("  %s: FAIL (%s)\n", name, what);
        return false;
    };

    {
        Book b;
        if (b.cancel_order(12345) != false) return fail("cancel unknown id");
    }
    {
        Book b;
        b.add_order(1, Side::Buy, 9900, 50);
        if (b.cancel_order(1) != true) return fail("first cancel");
        if (b.cancel_order(1) != false) return fail("second cancel");
    }
    {
        Book b;
        BBO x = b.get_bbo();
        if (x.has_bid || x.has_ask) return fail("empty book BBO");
    }
    {
        Book b;
        b.add_order(1, Side::Buy, 9800, 10);
        b.add_order(2, Side::Buy, 9700, 20);
        b.add_order(3, Side::Buy, 9600, 30);
        auto v = b.get_bids(10);
        if (v.size() != 3) return fail("insufficient depth size");
        if (v[0].price != 9800 || v[0].qty != 10) return fail("insufficient depth order");
    }
    {
        Book b;
        b.add_order(1, Side::Buy, 9900, 40);
        b.add_order(2, Side::Buy, 9900, 25);
        if (b.get_bbo().bid_qty != 65) return fail("level qty sum");
        if (b.cancel_order(1) != true) return fail("cancel one of two");
        BBO x = b.get_bbo();
        if (!x.has_bid || x.bid_qty != 25) return fail("qty after partial cancel");
    }
    {
        Book b;
        b.add_order(1, Side::Buy, 9900, 40);
        if (!b.get_bids(0).empty())   return fail("depth 0 not empty");
        if (!b.get_bids(-1).empty())  return fail("negative depth not empty");
        if (!b.get_asks(-5).empty())  return fail("negative depth asks not empty");
    }
    {
        Book b;
        b.add_order(1, Side::Sell, 10005, 10);
        b.add_order(2, Side::Sell, 10002, 20);
        b.add_order(3, Side::Sell, 10009, 30);
        auto v = b.get_asks(10);
        if (v.size() != 3) return fail("ask count");
        if (v[0].price != 10002 || v[1].price != 10005 || v[2].price != 10009)
            return fail("asks not sorted lowest-first");
        if (b.get_bbo().ask_price != 10002) return fail("best ask wrong");
        if (b.get_bids(10).size() != 0) return fail("asks leaked into bids");
    }
    {
        Book b;
        b.add_order(1, Side::Buy, 9900, 10);
        b.add_order(2, Side::Buy, 9800, 20);
        b.add_order(3, Side::Buy, 9700, 30);
        if (b.get_bbo().bid_price != 9900) return fail("best bid before cancel");
        if (b.cancel_order(1) != true) return fail("cancel best bid");
        BBO x = b.get_bbo();
        if (!x.has_bid || x.bid_price != 9800 || x.bid_qty != 20) return fail("best bid not promoted");
        if (b.get_bids(10).size() != 2) return fail("depth after best cancel");
        b.cancel_order(2);
        b.cancel_order(3);
        BBO y = b.get_bbo();
        if (y.has_bid) return fail("side not cleared when empty");
        if (b.get_bids(10).size() != 0) return fail("depth after emptying side");
    }
    {
        Book b;
        b.add_order(1, Side::Buy, BID_BASE, 1);
        b.add_order(2, Side::Buy, BID_MAX, 2);
        b.add_order(3, Side::Sell, ASK_BASE, 3);
        b.add_order(4, Side::Sell, ASK_MAX, 4);
        BBO x = b.get_bbo();
        if (x.bid_price != BID_MAX || x.ask_price != ASK_BASE) return fail("boundary BBO");
        auto v = b.get_asks(10);
        if (v.size() != 2 || v[1].price != ASK_MAX) return fail("boundary ask depth");
    }
    return true;
}

static void verify() {
    printf("Running correctness test...\n\n");

    const std::vector<Op> ops = build_workload(200000, 42);
    size_t fail_index = 0;

    bool a_edge = edge_cases<OrderBookA>("A");
    bool b_edge = edge_cases<OrderBookB>("B");
    bool c_edge = edge_cases<OrderBookC>("C");

    bool b_diff = compare_against_a<OrderBookB>(ops, fail_index);
    if (!b_diff) printf("  B differs from A at op %zu\n", fail_index);
    bool c_diff = compare_against_a<OrderBookC>(ops, fail_index);
    if (!c_diff) printf("  C differs from A at op %zu\n", fail_index);

    printf("Implementation A: %s\n", a_edge ? "PASS" : "FAIL");
    printf("Implementation B: %s\n", (b_edge && b_diff) ? "PASS" : "FAIL");
    printf("Implementation C: %s\n", (c_edge && c_diff) ? "PASS" : "FAIL");

    bool all = a_edge && b_edge && c_edge && b_diff && c_diff;
    printf("\n%s\n", all ? "All implementations produce identical results."
                          : "MISMATCH: implementations disagree.");
    exit(all ? 0 : 1);
}

int main(int argc, char** argv) {
    std::string mode;
    if (argc > 1) {
        mode = argv[1];
        if (mode == "--impl" && argc > 2) mode = mode + "=" + argv[2];
    }

    if (mode == "--verify") {
        verify();
        return 0;
    }

    const int n_ops = 1000000;
    const int runs = 5;
    printf("Building workload (%d ops, seed 42)...\n", n_ops);
    const std::vector<Op> ops = build_workload(n_ops, 42);

    if (mode == "--impl=A") { print_result("A", run<OrderBookA>(ops, runs)); return 0; }
    if (mode == "--impl=B") { print_result("B", run<OrderBookB>(ops, runs)); return 0; }
    if (mode == "--impl=C") { print_result("C", run<OrderBookC>(ops, runs)); return 0; }

    printf("Usage: %s --impl A|B|C | --verify\n", argv[0]);
    return 1;
}

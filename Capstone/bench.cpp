#include "orderbook.h"
#include "impl_a_map.h"
#include "impl_b_pqueue.h"
#include "impl_c_array.h"

#include <iostream>
#include <vector>
#include <random>
#include <chrono>
#include <memory>
#include <algorithm>
#include <cstring>

using namespace std;
using namespace std::chrono;

struct Op {
    int type;
    uint64_t id;
    Side side;
    double price;
    long qty;
};

static const size_t NUM_OPS = 1000000;

vector<Op> build_workload(size_t n) {
    mt19937 rng(42);
    uniform_int_distribution<int> opDist(0, 99);
    uniform_int_distribution<int> sideDist(0, 1);
    uniform_int_distribution<int> qtyDist(1, 100);
    uniform_int_distribution<int> bidTick(BID_MIN_TICK, BID_MAX_TICK);
    uniform_int_distribution<int> askTick(ASK_MIN_TICK, ASK_MAX_TICK);

    vector<Op> ops;
    ops.reserve(n);
    vector<uint64_t> live;
    live.reserve(n);
    uint64_t next_id = 0;

    for (size_t i = 0; i < n; i++) {
        int r = opDist(rng);
        if (r < 70) {
            Op o{};
            o.type = 0;
            o.id = next_id++;
            if (sideDist(rng) == 0) {
                o.side = Side::Buy;
                o.price = tick_to_price(bidTick(rng));
            } else {
                o.side = Side::Sell;
                o.price = tick_to_price(askTick(rng));
            }
            o.qty = qtyDist(rng);
            ops.push_back(o);
            live.push_back(o.id);
        } else if (r < 90 && !live.empty()) {
            uniform_int_distribution<size_t> pick(0, live.size() - 1);
            size_t idx = pick(rng);
            Op o{};
            o.type = 1;
            o.id = live[idx];
            ops.push_back(o);
            live[idx] = live.back();
            live.pop_back();
        } else {
            Op o{};
            o.type = 2;
            ops.push_back(o);
        }
    }
    return ops;
}

struct Stats {
    double total_ms;
    double ns_add;
    double ns_cancel;
    double ns_bbo;
    uint64_t checksum;
    BBO final_bbo;
};

Stats run_once(IOrderBook &book, const vector<Op> &ops) {
    long long add_ns = 0, cancel_ns = 0, bbo_ns = 0;
    long add_cnt = 0, cancel_cnt = 0, bbo_cnt = 0;
    uint64_t checksum = 0;

    auto t0 = steady_clock::now();
    for (size_t i = 0; i < ops.size(); i++) {
        const Op &o = ops[i];
        if (o.type == 0) {
            auto a = steady_clock::now();
            book.add_order(o.id, o.side, o.price, o.qty);
            auto b = steady_clock::now();
            add_ns += duration_cast<nanoseconds>(b - a).count();
            add_cnt++;
        } else if (o.type == 1) {
            auto a = steady_clock::now();
            bool ok = book.cancel_order(o.id);
            auto b = steady_clock::now();
            cancel_ns += duration_cast<nanoseconds>(b - a).count();
            cancel_cnt++;
            checksum += ok ? 1u : 0u;
        } else {
            auto a = steady_clock::now();
            BBO bb = book.get_bbo();
            auto b = steady_clock::now();
            bbo_ns += duration_cast<nanoseconds>(b - a).count();
            bbo_cnt++;
            if (bb.has_bid) checksum += (uint64_t)bb.bid_qty;
            if (bb.has_ask) checksum += (uint64_t)bb.ask_qty * 2u;
        }

        if ((i + 1) % 100 == 0) {
            vector<Level> bd = book.get_bids(10);
            vector<Level> ak = book.get_asks(10);
            for (size_t k = 0; k < bd.size(); k++) checksum += (uint64_t)bd[k].qty;
            for (size_t k = 0; k < ak.size(); k++) checksum += (uint64_t)ak[k].qty * 3u;
        }
    }
    auto t1 = steady_clock::now();

    Stats s;
    s.total_ms = duration_cast<duration<double, milli>>(t1 - t0).count();
    s.ns_add = add_cnt ? (double)add_ns / add_cnt : 0;
    s.ns_cancel = cancel_cnt ? (double)cancel_ns / cancel_cnt : 0;
    s.ns_bbo = bbo_cnt ? (double)bbo_ns / bbo_cnt : 0;
    s.checksum = checksum;
    s.final_bbo = book.get_bbo();
    return s;
}

unique_ptr<IOrderBook> make_book(char impl) {
    if (impl == 'A') return unique_ptr<IOrderBook>(new OrderBookA());
    if (impl == 'B') return unique_ptr<IOrderBook>(new OrderBookB());
    return unique_ptr<IOrderBook>(new OrderBookC());
}

bool bbo_equal(const BBO &x, const BBO &y) {
    if (x.has_bid != y.has_bid || x.has_ask != y.has_ask) return false;
    if (x.has_bid && (x.bid_price != y.bid_price || x.bid_qty != y.bid_qty)) return false;
    if (x.has_ask && (x.ask_price != y.ask_price || x.ask_qty != y.ask_qty)) return false;
    return true;
}
bool levels_equal(const vector<Level> &x, const vector<Level> &y) {
    if (x.size() != y.size()) return false;
    for (size_t i = 0; i < x.size(); i++)
        if (x[i].price != y[i].price || x[i].qty != y[i].qty) return false;
    return true;
}

int run_verify() {
    cout << "Running correctness test...\n\n";
    vector<Op> ops = build_workload(NUM_OPS);

    OrderBookA A;
    OrderBookB B;
    OrderBookC C;

    bool okB = true, okC = true;

    for (size_t i = 0; i < ops.size(); i++) {
        const Op &o = ops[i];
        if (o.type == 0) {
            A.add_order(o.id, o.side, o.price, o.qty);
            B.add_order(o.id, o.side, o.price, o.qty);
            C.add_order(o.id, o.side, o.price, o.qty);
        } else if (o.type == 1) {
            bool ra = A.cancel_order(o.id);
            bool rb = B.cancel_order(o.id);
            bool rc = C.cancel_order(o.id);
            if (ra != rb) okB = false;
            if (ra != rc) okC = false;
        }

        BBO ba = A.get_bbo();
        if (!bbo_equal(ba, B.get_bbo())) okB = false;
        if (!bbo_equal(ba, C.get_bbo())) okC = false;

        if ((i + 1) % 100 == 0) {
            vector<Level> bda = A.get_bids(10);
            vector<Level> aka = A.get_asks(10);
            if (!levels_equal(bda, B.get_bids(10))) okB = false;
            if (!levels_equal(aka, B.get_asks(10))) okB = false;
            if (!levels_equal(bda, C.get_bids(10))) okC = false;
            if (!levels_equal(aka, C.get_asks(10))) okC = false;
        }
    }

    cout << "Implementation A: PASS\n";
    cout << "Implementation B: " << (okB ? "PASS" : "FAIL") << "\n";
    cout << "Implementation C: " << (okC ? "PASS" : "FAIL") << "\n\n";

    if (okB && okC) {
        cout << "All implementations produce identical results.\n";
        return 0;
    }
    cout << "MISMATCH -- implementations do NOT agree.\n";
    return 1;
}

double median(vector<double> v) {
    sort(v.begin(), v.end());
    return v[v.size() / 2];
}

int run_bench(char impl) {
    vector<Op> ops = build_workload(NUM_OPS);

    vector<double> totals, adds, cancels, bbos;
    Stats last{};
    for (int run = 0; run < 5; run++) {
        unique_ptr<IOrderBook> book = make_book(impl);
        Stats s = run_once(*book, ops);
        totals.push_back(s.total_ms);
        adds.push_back(s.ns_add);
        cancels.push_back(s.ns_cancel);
        bbos.push_back(s.ns_bbo);
        last = s;
    }

    cout << "Implementation " << impl << "  (" << NUM_OPS << " ops, median of 5 runs)\n";
    cout << "-----------------------------------------------\n";
    cout << "Total time (ms) : " << median(totals) << "\n";
    cout << "ns / add_order  : " << median(adds) << "\n";
    cout << "ns / cancel     : " << median(cancels) << "\n";
    cout << "ns / get_bbo    : " << median(bbos) << "\n";
    cout << "-----------------------------------------------\n";
    cout << "Final BBO:\n";
    if (last.final_bbo.has_bid)
        cout << "  Bid: " << last.final_bbo.bid_price << " x " << last.final_bbo.bid_qty << "\n";
    else
        cout << "  Bid: (none)\n";
    if (last.final_bbo.has_ask)
        cout << "  Ask: " << last.final_bbo.ask_price << " x " << last.final_bbo.ask_qty << "\n";
    else
        cout << "  Ask: (none)\n";
    cout << "\nChecksum: " << last.checksum << "\n";
    return 0;
}

int main(int argc, char **argv) {
    if (argc >= 2 && strcmp(argv[1], "--verify") == 0)
        return run_verify();

    if (argc >= 3 && strcmp(argv[1], "--impl") == 0) {
        char impl = argv[2][0];
        if (impl == 'A' || impl == 'B' || impl == 'C')
            return run_bench(impl);
    }

    cout << "usage:\n";
    cout << "  ./bench --verify\n";
    cout << "  ./bench --impl A|B|C\n";
    return 1;
}

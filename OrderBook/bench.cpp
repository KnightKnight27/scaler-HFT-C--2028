// Unified benchmark + correctness harness for the three order books.
//
//   ./bench --impl A --ops 1000000 --runs 5 --seed 42
//   ./bench --impl B
//   ./bench --impl C
//   ./bench --verify --ops 100000 --seed 42
//
// Same workload (seed 42) is used for every implementation.

#include <iostream>
#include <vector>
#include <string>
#include <chrono>
#include <random>
#include <algorithm>
#include <iomanip>
#include <cstdint>
#include <cstdlib>
#include <cctype>
#include <sys/resource.h>

#include "order.h"
#include "impl_a/orderbook_a.h"
#include "impl_b/orderbook_b.h"
#include "impl_c/orderbook_c.h"

using namespace std;

enum OpKind { OP_ADD, OP_CANCEL, OP_BBO, OP_DEPTH };

struct Op {
    OpKind kind;
    Side side;
    double price;
    long qty;
    uint64_t id;
};

// ---------------------------------------------------------------------------
// workload
// ---------------------------------------------------------------------------
// 70% add / 20% cancel / 10% bbo, and a depth query every 100 ops.
// Cancel always picks an id that is still live.
vector<Op> make_workload(long nops, unsigned seed) {
    mt19937 rng(seed);
    uniform_int_distribution<int> roll(1, 100);
    uniform_int_distribution<int> side_pick(0, 1);
    uniform_int_distribution<int> qty_pick(1, 100);
    uniform_int_distribution<long> bid_tick(5000, 9999);    // 50.00 .. 99.99
    uniform_int_distribution<long> ask_tick(10001, 15000);  // 100.01 .. 150.00

    vector<Op> ops;
    vector<uint64_t> live;
    uint64_t next_id = 1;

    for (long i = 1; i <= nops; ++i) {
        int r = roll(rng);
        if (r <= 70 || live.empty()) {
            Side s = (side_pick(rng) == 0) ? Side::Buy : Side::Sell;
            long t = (s == Side::Buy) ? bid_tick(rng) : ask_tick(rng);
            Op o;
            o.kind = OP_ADD;
            o.side = s;
            o.price = t / 100.0;
            o.qty = qty_pick(rng);
            o.id = next_id++;
            live.push_back(o.id);
            ops.push_back(o);
        } else if (r <= 90) {
            uniform_int_distribution<size_t> pick(0, live.size() - 1);
            size_t k = pick(rng);
            Op o;
            o.kind = OP_CANCEL;
            o.side = Side::Buy;
            o.price = 0.0;
            o.qty = 0;
            o.id = live[k];
            live[k] = live.back();
            live.pop_back();
            ops.push_back(o);
        } else {
            Op o;
            o.kind = OP_BBO;
            o.side = Side::Buy;
            o.price = 0.0;
            o.qty = 0;
            o.id = 0;
            ops.push_back(o);
        }

        if (i % 100 == 0) {
            Op o;
            o.kind = OP_DEPTH;
            o.side = Side::Buy;
            o.price = 0.0;
            o.qty = 0;
            o.id = 0;
            ops.push_back(o);
        }
    }
    return ops;
}

// ---------------------------------------------------------------------------
// single benchmark run
// ---------------------------------------------------------------------------
struct RunResult {
    double total_ms;
    double ns_add;
    double ns_cancel;
    double ns_bbo;
    long page_faults;
    BBO final_bbo;
    unsigned long long checksum;
};

RunResult run_once(OrderBook* book, const vector<Op>& ops) {
    unsigned long long add_ns = 0, cancel_ns = 0, bbo_ns = 0;
    long n_add = 0, n_cancel = 0, n_bbo = 0;
    unsigned long long checksum = 0;

    struct rusage ru0, ru1;
    getrusage(RUSAGE_SELF, &ru0);

    auto start = chrono::steady_clock::now();

    for (size_t i = 0; i < ops.size(); ++i) {
        const Op& o = ops[i];
        if (o.kind == OP_ADD) {
            auto a = chrono::steady_clock::now();
            book->add_order(o.id, o.side, o.price, o.qty);
            auto b = chrono::steady_clock::now();
            add_ns += chrono::duration_cast<chrono::nanoseconds>(b - a).count();
            n_add++;
        } else if (o.kind == OP_CANCEL) {
            auto a = chrono::steady_clock::now();
            book->cancel_order(o.id);
            auto b = chrono::steady_clock::now();
            cancel_ns += chrono::duration_cast<chrono::nanoseconds>(b - a).count();
            n_cancel++;
        } else if (o.kind == OP_BBO) {
            auto a = chrono::steady_clock::now();
            BBO bb = book->get_bbo();
            auto b = chrono::steady_clock::now();
            bbo_ns += chrono::duration_cast<chrono::nanoseconds>(b - a).count();
            n_bbo++;
            if (bb.has_bid) checksum += bb.bid_qty + (unsigned long long)(bb.bid_price * 100);
            if (bb.has_ask) checksum += bb.ask_qty + (unsigned long long)(bb.ask_price * 100);
        } else {
            vector<Level> bids = book->get_bids(10);
            vector<Level> asks = book->get_asks(10);
            for (size_t k = 0; k < bids.size(); ++k) checksum += bids[k].qty;
            for (size_t k = 0; k < asks.size(); ++k) checksum += asks[k].qty;
        }
    }

    auto end = chrono::steady_clock::now();
    getrusage(RUSAGE_SELF, &ru1);

    RunResult r;
    r.total_ms = chrono::duration<double, milli>(end - start).count();
    r.ns_add = (n_add > 0) ? (double)add_ns / n_add : 0.0;
    r.ns_cancel = (n_cancel > 0) ? (double)cancel_ns / n_cancel : 0.0;
    r.ns_bbo = (n_bbo > 0) ? (double)bbo_ns / n_bbo : 0.0;
    r.page_faults = (ru1.ru_minflt + ru1.ru_majflt) - (ru0.ru_minflt + ru0.ru_majflt);
    r.final_bbo = book->get_bbo();
    r.checksum = checksum;
    return r;
}

OrderBook* make_book(char which) {
    if (which == 'A') return new OrderBookA();
    if (which == 'B') return new OrderBookB();
    if (which == 'C') return new OrderBookC();
    return NULL;
}

double median(vector<double> v) {
    if (v.empty()) return 0.0;
    sort(v.begin(), v.end());
    size_t n = v.size();
    if (n % 2 == 1) return v[n / 2];
    return (v[n / 2 - 1] + v[n / 2]) / 2.0;
}

// ---------------------------------------------------------------------------
// correctness
// ---------------------------------------------------------------------------
bool same_bbo(const BBO& x, const BBO& y) {
    if (x.has_bid != y.has_bid || x.has_ask != y.has_ask) return false;
    if (x.has_bid) {
        if (fabs(x.bid_price - y.bid_price) > 1e-6 || x.bid_qty != y.bid_qty) return false;
    }
    if (x.has_ask) {
        if (fabs(x.ask_price - y.ask_price) > 1e-6 || x.ask_qty != y.ask_qty) return false;
    }
    return true;
}

bool same_levels(const vector<Level>& x, const vector<Level>& y) {
    if (x.size() != y.size()) return false;
    for (size_t i = 0; i < x.size(); ++i) {
        if (fabs(x[i].price - y[i].price) > 1e-6 || x[i].qty != y[i].qty) return false;
    }
    return true;
}

bool run_verify(long nops, unsigned seed) {
    cout << "Running correctness test over " << nops << " operations (seed " << seed << ") ..." << endl;

    vector<Op> ops = make_workload(nops, seed);
    OrderBookA a;
    OrderBookB b;
    OrderBookC c;

    bool ok = true;

    for (size_t i = 0; i < ops.size() && ok; ++i) {
        const Op& o = ops[i];
        if (o.kind == OP_ADD) {
            a.add_order(o.id, o.side, o.price, o.qty);
            b.add_order(o.id, o.side, o.price, o.qty);
            c.add_order(o.id, o.side, o.price, o.qty);
        } else if (o.kind == OP_CANCEL) {
            bool ra = a.cancel_order(o.id);
            bool rb = b.cancel_order(o.id);
            bool rc = c.cancel_order(o.id);
            if (ra != rb || ra != rc) {
                cout << "cancel mismatch at op " << i << " (id " << o.id << "): "
                     << ra << "/" << rb << "/" << rc << endl;
                ok = false;
            }
        }

        if (!ok) break;

        if (!same_bbo(a.get_bbo(), b.get_bbo()) || !same_bbo(a.get_bbo(), c.get_bbo())) {
            cout << "BBO mismatch at op " << i << endl;
            ok = false;
            break;
        }

        if (o.kind == OP_DEPTH || i % 100 == 0) {
            if (!same_levels(a.get_bids(10), b.get_bids(10)) ||
                !same_levels(a.get_bids(10), c.get_bids(10)) ||
                !same_levels(a.get_asks(10), b.get_asks(10)) ||
                !same_levels(a.get_asks(10), c.get_asks(10))) {
                cout << "depth mismatch at op " << i << endl;
                ok = false;
                break;
            }
        }
    }

    cout << endl;
    cout << "Implementation A: " << (ok ? "PASS" : "FAIL") << endl;
    cout << "Implementation B: " << (ok ? "PASS" : "FAIL") << endl;
    cout << "Implementation C: " << (ok ? "PASS" : "FAIL") << endl;
    cout << endl;
    if (ok) {
        cout << "All implementations produce identical results." << endl;
    } else {
        cout << "Verification FAILED." << endl;
    }
    return ok;
}

// ---------------------------------------------------------------------------
void usage(const char* prog) {
    cout << "usage: " << prog << " [--impl A|B|C] [--verify] [--ops N] [--runs N] [--seed S]" << endl;
}

int main(int argc, char** argv) {
    string impl = "";
    bool verify = false;
    long nops = 1000000;
    long nruns = 5;
    unsigned seed = 42;

    for (int i = 1; i < argc; ++i) {
        string arg = argv[i];
        if (arg == "--impl" && i + 1 < argc) {
            impl = argv[++i];
        } else if (arg == "--verify") {
            verify = true;
        } else if (arg == "--ops" && i + 1 < argc) {
            nops = atol(argv[++i]);
        } else if (arg == "--runs" && i + 1 < argc) {
            nruns = atol(argv[++i]);
        } else if (arg == "--seed" && i + 1 < argc) {
            seed = (unsigned)atol(argv[++i]);
        } else if (arg == "--help" || arg == "-h") {
            usage(argv[0]);
            return 0;
        } else {
            cerr << "unknown argument: " << arg << endl;
            usage(argv[0]);
            return 1;
        }
    }

    if (verify) {
        if (run_verify(nops, seed)) {
            return 0;
        }
        return 1;
    }

    if (impl.empty()) {
        cerr << "need --impl A|B|C or --verify" << endl;
        usage(argv[0]);
        return 1;
    }

    char which = (char)toupper(impl[0]);
    if (which != 'A' && which != 'B' && which != 'C') {
        cerr << "unknown implementation: " << impl << endl;
        return 1;
    }

    cout << "===============================================" << endl;
    cout << "OrderBook benchmark, implementation " << which << endl;
    cout << "ops = " << nops << "  runs = " << nruns << "  seed = " << seed << endl;
    cout << "===============================================" << endl;
    cout << "generating workload ... " << flush;
    vector<Op> ops = make_workload(nops, seed);
    cout << "done (" << ops.size() << " ops)" << endl << endl;

    vector<double> totals, adds, cancels, bbos, faults;
    RunResult last;

    for (long r = 1; r <= nruns; ++r) {
        OrderBook* book = make_book(which);
        last = run_once(book, ops);
        delete book;

        totals.push_back(last.total_ms);
        adds.push_back(last.ns_add);
        cancels.push_back(last.ns_cancel);
        bbos.push_back(last.ns_bbo);
        faults.push_back((double)last.page_faults);

        cout << "run " << r << ": " << fixed << setprecision(2) << last.total_ms << " ms | "
             << "add " << setprecision(1) << last.ns_add << " ns | "
             << "cancel " << last.ns_cancel << " ns | "
             << "bbo " << last.ns_bbo << " ns | "
             << "faults " << last.page_faults << endl;
    }

    cout << endl << "------------- medians over " << nruns << " runs -------------" << endl;
    cout << "total time      : " << fixed << setprecision(2) << median(totals) << " ms" << endl;
    cout << "ns / add_order  : " << setprecision(1) << median(adds) << endl;
    cout << "ns / cancel     : " << median(cancels) << endl;
    cout << "ns / get_bbo    : " << median(bbos) << endl;
    cout << "page faults     : " << (long)median(faults) << endl;
    cout << "-------------------------------------------------------------" << endl;

    cout << "final BBO:" << endl;
    if (last.final_bbo.has_bid) {
        cout << "  bid: " << fixed << setprecision(2) << last.final_bbo.bid_price
             << " x " << last.final_bbo.bid_qty << endl;
    } else {
        cout << "  bid: none" << endl;
    }
    if (last.final_bbo.has_ask) {
        cout << "  ask: " << last.final_bbo.ask_price << " x " << last.final_bbo.ask_qty << endl;
    } else {
        cout << "  ask: none" << endl;
    }
    cout << "checksum: " << last.checksum << endl;
    return 0;
}

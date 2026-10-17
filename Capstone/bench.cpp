#include <iostream>
#include <cstdio>
#include <vector>
#include <string>
#include <random>
#include <chrono>
#include <algorithm>
#include "book_a.h"
#include "book_b.h"
#include "book_c.h"

using namespace std;

const int NUM_OPS = 1000000;

enum OpType { ADD, CANCEL, QUERY };

struct Op {
    OpType type;
    uint64_t id;
    Side side;
    double price;
    long qty;
};

struct Result {
    double total_ms;
    double add_ns;
    double cancel_ns;
    double bbo_ns;
    long long checksum;
    BBO last;
};

vector<Op> make_ops(int n) {
    mt19937 rng(42);
    uniform_int_distribution<int> pick(1, 100);
    uniform_int_distribution<int> side_pick(0, 1);
    uniform_int_distribution<int> qty_pick(1, 100);
    uniform_int_distribution<int> bid_pick(MIN_BID, MAX_BID);
    uniform_int_distribution<int> ask_pick(MIN_ASK, MAX_ASK);

    vector<Op> ops;
    ops.reserve(n);
    vector<uint64_t> live;
    uint64_t next_id = 1;

    for (int i = 0; i < n; i++) {
        int r = pick(rng);
        Op op;
        op.id = 0;
        op.side = Side::Buy;
        op.price = 0;
        op.qty = 0;

        if (r <= 70 || (r <= 90 && live.empty())) {
            op.type = ADD;
            op.id = next_id;
            next_id++;
            if (side_pick(rng) == 0) {
                op.side = Side::Buy;
                op.price = bid_pick(rng) / 100.0;
            } else {
                op.side = Side::Sell;
                op.price = ask_pick(rng) / 100.0;
            }
            op.qty = qty_pick(rng);
            live.push_back(op.id);
        } else if (r <= 90) {
            op.type = CANCEL;
            uniform_int_distribution<size_t> idx_pick(0, live.size() - 1);
            size_t k = idx_pick(rng);
            op.id = live[k];
            live[k] = live.back();
            live.pop_back();
        } else {
            op.type = QUERY;
        }
        ops.push_back(op);
    }
    return ops;
}

long long ns_between(chrono::steady_clock::time_point a, chrono::steady_clock::time_point b) {
    return chrono::duration_cast<chrono::nanoseconds>(b - a).count();
}

template <class Book>
Result run_once(const vector<Op>& ops) {
    Book book;
    long long add_time = 0, cancel_time = 0, bbo_time = 0;
    long long adds = 0, cancels = 0, bbos = 0;
    long long checksum = 0;

    auto start = chrono::steady_clock::now();
    for (size_t i = 0; i < ops.size(); i++) {
        const Op& op = ops[i];
        if (op.type == ADD) {
            auto t1 = chrono::steady_clock::now();
            book.add_order(op.id, op.side, op.price, op.qty);
            auto t2 = chrono::steady_clock::now();
            add_time += ns_between(t1, t2);
            adds++;
        } else if (op.type == CANCEL) {
            auto t1 = chrono::steady_clock::now();
            bool ok = book.cancel_order(op.id);
            auto t2 = chrono::steady_clock::now();
            cancel_time += ns_between(t1, t2);
            cancels++;
            if (ok) {
                checksum += 1;
            }
        } else {
            auto t1 = chrono::steady_clock::now();
            BBO b = book.get_bbo();
            auto t2 = chrono::steady_clock::now();
            bbo_time += ns_between(t1, t2);
            bbos++;
            checksum += b.bid_qty + b.ask_qty;
        }

        if ((i + 1) % 100 == 0) {
            vector<Level> bid_levels = book.get_bids(10);
            vector<Level> ask_levels = book.get_asks(10);
            for (size_t j = 0; j < bid_levels.size(); j++) {
                checksum += bid_levels[j].qty;
            }
            for (size_t j = 0; j < ask_levels.size(); j++) {
                checksum += ask_levels[j].qty;
            }
        }
    }
    auto end = chrono::steady_clock::now();

    Result res;
    res.total_ms = ns_between(start, end) / 1e6;
    res.add_ns = adds > 0 ? (double)add_time / adds : 0;
    res.cancel_ns = cancels > 0 ? (double)cancel_time / cancels : 0;
    res.bbo_ns = bbos > 0 ? (double)bbo_time / bbos : 0;
    res.checksum = checksum;
    res.last = book.get_bbo();
    return res;
}

double median(vector<double> v) {
    sort(v.begin(), v.end());
    int n = v.size();
    if (n % 2 == 1) {
        return v[n / 2];
    }
    return (v[n / 2 - 1] + v[n / 2]) / 2.0;
}

bool same_bbo(const BBO& x, const BBO& y) {
    if (x.has_bid != y.has_bid || x.has_ask != y.has_ask) {
        return false;
    }
    if (x.has_bid && (x.bid_price != y.bid_price || x.bid_qty != y.bid_qty)) {
        return false;
    }
    if (x.has_ask && (x.ask_price != y.ask_price || x.ask_qty != y.ask_qty)) {
        return false;
    }
    return true;
}

bool same_levels(const vector<Level>& x, const vector<Level>& y) {
    if (x.size() != y.size()) {
        return false;
    }
    for (size_t i = 0; i < x.size(); i++) {
        if (x[i].price != y[i].price || x[i].qty != y[i].qty) {
            return false;
        }
    }
    return true;
}

template <class Book>
bool edge_tests() {
    Book book;

    BBO b = book.get_bbo();
    if (b.has_bid || b.has_ask) return false;
    if (!book.get_bids(10).empty() || !book.get_asks(10).empty()) return false;
    if (book.cancel_order(12345)) return false;

    book.add_order(1, Side::Buy, 99.50, 10);
    book.add_order(2, Side::Buy, 99.50, 5);
    book.add_order(3, Side::Buy, 99.00, 7);
    book.add_order(4, Side::Buy, 98.25, 3);
    book.add_order(5, Side::Sell, 100.50, 20);

    vector<Level> bids = book.get_bids(10);
    if (bids.size() != 3) return false;
    if (bids[0].price != 99.50 || bids[0].qty != 15) return false;
    if (bids[1].price != 99.00 || bids[1].qty != 7) return false;
    if (bids[2].price != 98.25 || bids[2].qty != 3) return false;

    vector<Level> asks = book.get_asks(10);
    if (asks.size() != 1 || asks[0].price != 100.50 || asks[0].qty != 20) return false;

    b = book.get_bbo();
    if (!b.has_bid || b.bid_price != 99.50 || b.bid_qty != 15) return false;
    if (!b.has_ask || b.ask_price != 100.50 || b.ask_qty != 20) return false;

    if (!book.cancel_order(1)) return false;
    if (book.cancel_order(1)) return false;
    b = book.get_bbo();
    if (b.bid_price != 99.50 || b.bid_qty != 5) return false;

    if (!book.cancel_order(2)) return false;
    b = book.get_bbo();
    if (b.bid_price != 99.00 || b.bid_qty != 7) return false;

    if (!book.cancel_order(5)) return false;
    b = book.get_bbo();
    if (b.has_ask) return false;

    book.add_order(6, Side::Buy, 120.00, 5);
    book.add_order(7, Side::Sell, 60.00, 5);
    book.add_order(8, Side::Buy, 99.10, 0);
    if (book.get_bids(10).size() != 2) return false;
    if (!book.get_asks(10).empty()) return false;
    if (book.cancel_order(6)) return false;

    return true;
}

int run_verify() {
    cout << "Running correctness test..." << endl << endl;

    bool ok_a = edge_tests<BookA>();
    bool ok_b = edge_tests<BookB>();
    bool ok_c = edge_tests<BookC>();
    cout << "edge cases  A: " << (ok_a ? "ok" : "FAILED")
         << "  B: " << (ok_b ? "ok" : "FAILED")
         << "  C: " << (ok_c ? "ok" : "FAILED") << endl;

    vector<Op> ops = make_ops(NUM_OPS);
    BookA a;
    BookB b;
    BookC c;
    long long first_bad_a = -1, first_bad_b = -1, first_bad_c = -1;

    for (size_t i = 0; i < ops.size(); i++) {
        const Op& op = ops[i];
        if (op.type == ADD) {
            a.add_order(op.id, op.side, op.price, op.qty);
            b.add_order(op.id, op.side, op.price, op.qty);
            c.add_order(op.id, op.side, op.price, op.qty);
        } else if (op.type == CANCEL) {
            bool ra = a.cancel_order(op.id);
            bool rb = b.cancel_order(op.id);
            bool rc = c.cancel_order(op.id);
            if (!ra && first_bad_a == -1) first_bad_a = i;
            if (rb != ra && first_bad_b == -1) first_bad_b = i;
            if (rc != ra && first_bad_c == -1) first_bad_c = i;

            bool again_a = a.cancel_order(op.id);
            bool again_b = b.cancel_order(op.id);
            bool again_c = c.cancel_order(op.id);
            if (again_a && first_bad_a == -1) first_bad_a = i;
            if (again_b && first_bad_b == -1) first_bad_b = i;
            if (again_c && first_bad_c == -1) first_bad_c = i;
        }

        BBO x = a.get_bbo();
        if (first_bad_b == -1 && !same_bbo(x, b.get_bbo())) first_bad_b = i;
        if (first_bad_c == -1 && !same_bbo(x, c.get_bbo())) first_bad_c = i;

        vector<Level> bids_a = a.get_bids(10);
        vector<Level> asks_a = a.get_asks(10);
        if (first_bad_b == -1 && (!same_levels(bids_a, b.get_bids(10)) || !same_levels(asks_a, b.get_asks(10)))) first_bad_b = i;
        if (first_bad_c == -1 && (!same_levels(bids_a, c.get_bids(10)) || !same_levels(asks_a, c.get_asks(10)))) first_bad_c = i;

        if (i % 10000 == 0) {
            vector<Level> all_bids = a.get_bids(100000);
            vector<Level> all_asks = a.get_asks(100000);
            if (first_bad_b == -1 && (!same_levels(all_bids, b.get_bids(100000)) || !same_levels(all_asks, b.get_asks(100000)))) first_bad_b = i;
            if (first_bad_c == -1 && (!same_levels(all_bids, c.get_bids(100000)) || !same_levels(all_asks, c.get_asks(100000)))) first_bad_c = i;
        }
    }

    cout << "workload: " << ops.size() << " ops, BBO + top 10 levels checked after every op, full book every 10000 ops" << endl;
    cout << "final levels  bids: " << a.get_bids(100000).size() << "  asks: " << a.get_asks(100000).size() << endl << endl;

    bool pass_a = ok_a && first_bad_a == -1;
    bool pass_b = ok_b && first_bad_b == -1;
    bool pass_c = ok_c && first_bad_c == -1;

    cout << "Implementation A: " << (pass_a ? "PASS" : "FAIL");
    if (first_bad_a != -1) cout << " (bad cancel at op " << first_bad_a << ")";
    cout << endl;
    cout << "Implementation B: " << (pass_b ? "PASS" : "FAIL");
    if (first_bad_b != -1) cout << " (differs from A at op " << first_bad_b << ")";
    cout << endl;
    cout << "Implementation C: " << (pass_c ? "PASS" : "FAIL");
    if (first_bad_c != -1) cout << " (differs from A at op " << first_bad_c << ")";
    cout << endl << endl;

    if (pass_a && pass_b && pass_c) {
        cout << "All implementations produce identical results." << endl;
        return 0;
    }
    cout << "Implementations do NOT agree." << endl;
    return 1;
}

void print_usage() {
    cout << "usage:" << endl;
    cout << "  ./bench --verify" << endl;
    cout << "  ./bench --impl A|B|C [--runs N]" << endl;
}

int main(int argc, char** argv) {
    string impl = "";
    bool verify = false;
    int runs = 5;

    for (int i = 1; i < argc; i++) {
        string arg = argv[i];
        if (arg == "--verify") {
            verify = true;
        } else if (arg == "--impl" && i + 1 < argc) {
            impl = argv[i + 1];
            i++;
        } else if (arg == "--runs" && i + 1 < argc) {
            runs = stoi(argv[i + 1]);
            i++;
        } else {
            print_usage();
            return 1;
        }
    }

    if (verify) {
        return run_verify();
    }
    if ((impl != "A" && impl != "B" && impl != "C") || runs < 1) {
        print_usage();
        return 1;
    }

    vector<Op> ops = make_ops(NUM_OPS);
    vector<double> totals, adds, cancels, bbos;
    Result res;

    cout << "Implementation " << impl << ", " << ops.size() << " ops, " << runs << " runs" << endl;
    for (int r = 0; r < runs; r++) {
        if (impl == "A") {
            res = run_once<BookA>(ops);
        } else if (impl == "B") {
            res = run_once<BookB>(ops);
        } else {
            res = run_once<BookC>(ops);
        }
        printf("run %d: total %.2f ms | add %.1f ns | cancel %.1f ns | bbo %.1f ns\n",
               r + 1, res.total_ms, res.add_ns, res.cancel_ns, res.bbo_ns);
        totals.push_back(res.total_ms);
        adds.push_back(res.add_ns);
        cancels.push_back(res.cancel_ns);
        bbos.push_back(res.bbo_ns);
    }

    cout << endl << "Median:" << endl;
    printf("  total time  : %.2f ms\n", median(totals));
    printf("  ns / add    : %.1f\n", median(adds));
    printf("  ns / cancel : %.1f\n", median(cancels));
    printf("  ns / bbo    : %.1f\n", median(bbos));

    cout << endl << "Final BBO:" << endl;
    if (res.last.has_bid) {
        printf("Bid: %.2f x %ld\n", res.last.bid_price, res.last.bid_qty);
    } else {
        printf("Bid: none\n");
    }
    if (res.last.has_ask) {
        printf("Ask: %.2f x %ld\n", res.last.ask_price, res.last.ask_qty);
    } else {
        printf("Ask: none\n");
    }
    cout << endl << "Checksum: " << res.checksum << endl;
    return 0;
}

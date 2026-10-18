// Standalone correctness checker.
//
//   ./verify            (25000 random ops)
//   ./verify --ops 100000
//
// Runs a few hand written unit tests and then a random workload through all
// three books, checking that BBO and depth come out the same everywhere.

#include <iostream>
#include <vector>
#include <string>
#include <random>
#include <cstdlib>
#include <cmath>

#include "impl_a/orderbook_a.h"
#include "impl_b/orderbook_b.h"
#include "impl_c/orderbook_c.h"

using namespace std;

static int checks = 0;
static int fails = 0;

void check(bool cond, const string& what) {
    checks++;
    if (!cond) {
        fails++;
        cout << "  FAIL: " << what << endl;
    }
}

bool same_bbo(const BBO& x, const BBO& y) {
    if (x.has_bid != y.has_bid || x.has_ask != y.has_ask) return false;
    if (x.has_bid && (fabs(x.bid_price - y.bid_price) > 1e-9 || x.bid_qty != y.bid_qty)) return false;
    if (x.has_ask && (fabs(x.ask_price - y.ask_price) > 1e-9 || x.ask_qty != y.ask_qty)) return false;
    return true;
}

bool same_levels(const vector<Level>& x, const vector<Level>& y) {
    if (x.size() != y.size()) return false;
    for (size_t i = 0; i < x.size(); ++i) {
        if (fabs(x[i].price - y[i].price) > 1e-9 || x[i].qty != y[i].qty) return false;
    }
    return true;
}

// run one unit test body on all three books and make sure they agree after
void for_all(const string& name, void (*body)(OrderBook&)) {
    OrderBookA a;
    OrderBookB b;
    OrderBookC c;
    body(a);
    body(b);
    body(c);
    check(same_bbo(a.get_bbo(), b.get_bbo()), name + " : A/B bbo");
    check(same_bbo(a.get_bbo(), c.get_bbo()), name + " : A/C bbo");
    check(same_levels(a.get_bids(10), b.get_bids(10)), name + " : A/B bids");
    check(same_levels(a.get_bids(10), c.get_bids(10)), name + " : A/C bids");
    check(same_levels(a.get_asks(10), b.get_asks(10)), name + " : A/B asks");
    check(same_levels(a.get_asks(10), c.get_asks(10)), name + " : A/C asks");
}

// ---- unit tests ----
void t_empty(OrderBook& ob) {
    BBO b = ob.get_bbo();
    check(!b.has_bid && !b.has_ask, "empty book has no bbo");
    check(ob.get_bids(10).empty(), "empty bids");
    check(ob.get_asks(10).empty(), "empty asks");
    check(ob.cancel_order(1) == false, "cancel unknown returns false");
}

void t_single_buy(OrderBook& ob) {
    ob.add_order(1, Side::Buy, 99.00, 50);
    BBO b = ob.get_bbo();
    check(b.has_bid && !b.has_ask, "single buy flags");
    check(fabs(b.bid_price - 99.00) < 1e-9 && b.bid_qty == 50, "single buy values");
}

void t_same_price(OrderBook& ob) {
    ob.add_order(1, Side::Buy, 99.00, 50);
    ob.add_order(2, Side::Buy, 99.00, 25);
    check(ob.get_bbo().bid_qty == 75, "qty adds up at same price");
}

void t_better_bid(OrderBook& ob) {
    ob.add_order(1, Side::Buy, 99.00, 50);
    ob.add_order(2, Side::Buy, 99.50, 40);
    BBO b = ob.get_bbo();
    check(fabs(b.bid_price - 99.50) < 1e-9 && b.bid_qty == 40, "better bid wins");
}

void t_asks(OrderBook& ob) {
    ob.add_order(1, Side::Sell, 101.00, 100);
    ob.add_order(2, Side::Sell, 100.50, 60);
    BBO b = ob.get_bbo();
    check(b.has_ask && fabs(b.ask_price - 100.50) < 1e-9 && b.ask_qty == 60, "best ask is lowest");
}

void t_depth(OrderBook& ob) {
    ob.add_order(1, Side::Buy, 99.00, 50);
    ob.add_order(2, Side::Buy, 99.50, 40);
    ob.add_order(3, Side::Buy, 98.00, 10);
    vector<Level> bids = ob.get_bids(10);
    check(bids.size() == 3, "only 3 levels returned when book has 3");
    check(fabs(bids[0].price - 99.50) < 1e-9, "depth sorted best first");
    check(ob.get_bids(1).size() == 1, "depth 1 returns 1");
    check(ob.get_bids(0).empty(), "depth 0 empty");
}

void t_cancel(OrderBook& ob) {
    ob.add_order(1, Side::Buy, 99.00, 50);
    ob.add_order(2, Side::Buy, 99.50, 40);
    ob.add_order(3, Side::Buy, 99.00, 25);
    check(ob.cancel_order(2) == true, "cancel known works");
    check(ob.cancel_order(2) == false, "double cancel returns false");
    BBO b = ob.get_bbo();
    check(fabs(b.bid_price - 99.00) < 1e-9 && b.bid_qty == 75, "bbo after cancel");
    check(ob.cancel_order(1) == true, "cancel one from a level");
    check(ob.get_bbo().bid_qty == 25, "qty down to 25");
    check(ob.cancel_order(3) == true, "cancel last");
    check(!ob.get_bbo().has_bid, "book empty again");
}

// ---- random cross check ----
bool random_check(long nops, unsigned seed) {
    OrderBookA a;
    OrderBookB b;
    OrderBookC c;

    mt19937 rng(seed);
    uniform_int_distribution<int> roll(1, 100);
    uniform_int_distribution<int> side_pick(0, 1);
    uniform_int_distribution<int> qty_pick(1, 100);
    uniform_int_distribution<long> bid_tick(5000, 9999);
    uniform_int_distribution<long> ask_tick(10001, 15000);

    vector<uint64_t> live;
    uint64_t next_id = 1;

    for (long i = 1; i <= nops; ++i) {
        int r = roll(rng);
        if (r <= 70 || live.empty()) {
            Side s = (side_pick(rng) == 0) ? Side::Buy : Side::Sell;
            long t = (s == Side::Buy) ? bid_tick(rng) : ask_tick(rng);
            double p = t / 100.0;
            long q = qty_pick(rng);
            uint64_t id = next_id++;
            a.add_order(id, s, p, q);
            b.add_order(id, s, p, q);
            c.add_order(id, s, p, q);
            live.push_back(id);
        } else if (r <= 90) {
            uniform_int_distribution<size_t> pick(0, live.size() - 1);
            size_t k = pick(rng);
            uint64_t id = live[k];
            bool ra = a.cancel_order(id);
            bool rb = b.cancel_order(id);
            bool rc = c.cancel_order(id);
            if (ra != rb || ra != rc) {
                cout << "FAIL: cancel return mismatch at op " << i << endl;
                return false;
            }
            live[k] = live.back();
            live.pop_back();
        } else {
            if (!same_bbo(a.get_bbo(), b.get_bbo()) || !same_bbo(a.get_bbo(), c.get_bbo())) {
                cout << "FAIL: bbo mismatch at op " << i << endl;
                return false;
            }
        }

        if (i % 100 == 0) {
            if (!same_levels(a.get_bids(10), b.get_bids(10)) ||
                !same_levels(a.get_bids(10), c.get_bids(10)) ||
                !same_levels(a.get_asks(10), b.get_asks(10)) ||
                !same_levels(a.get_asks(10), c.get_asks(10))) {
                cout << "FAIL: depth mismatch at op " << i << endl;
                return false;
            }
        }
    }
    if (!same_bbo(a.get_bbo(), b.get_bbo()) || !same_bbo(a.get_bbo(), c.get_bbo())) return false;
    return true;
}

int main(int argc, char** argv) {
    long nops = 25000;
    for (int i = 1; i < argc; ++i) {
        string arg = argv[i];
        if (arg == "--ops" && i + 1 < argc) {
            nops = atol(argv[++i]);
        } else if (arg == "--verify" || arg == "-v") {
            // accepted for the assignment
        } else if (arg == "--help" || arg == "-h") {
            cout << "usage: " << argv[0] << " [--verify] [--ops N]" << endl;
            return 0;
        }
    }

    cout << "Running correctness test..." << endl << endl;

    for_all("empty", t_empty);
    for_all("single buy", t_single_buy);
    for_all("same price", t_same_price);
    for_all("better bid", t_better_bid);
    for_all("asks", t_asks);
    for_all("depth", t_depth);
    for_all("cancel", t_cancel);

    cout << "unit tests: " << (checks - fails) << "/" << checks << " passed" << endl << endl;

    bool rnd = random_check(nops, 42);
    if (!rnd) {
        cout << endl << "Randomized verification failed!" << endl;
        return 1;
    }

    if (fails > 0) {
        return 1;
    }

    cout << "Implementation A: PASS" << endl;
    cout << "Implementation B: PASS" << endl;
    cout << "Implementation C: PASS" << endl << endl;
    cout << "All implementations produce identical results." << endl;
    return 0;
}

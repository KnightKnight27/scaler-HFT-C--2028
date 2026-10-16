#include <iostream>
#include <vector>
#include <random>
#include <chrono>
#include <cstdint>
#include <algorithm>
#include <iomanip>
#include <string>
#include <memory>
#include <cmath>
#include <cstdio>
#include <tuple>
#include "order_book.h"
#include "order_book_implA.h"
#include "order_book_implB.h"
#include "order_book_implC.h"

using namespace std;

struct LiveOrder {
    uint64_t id;
    Side side;
    double price;
    long qty;
};

struct Operation {
    enum Type { ADD, CANCEL, BBO_Q } type;
    uint64_t id;
    Side side;
    double price;
    long qty;
};

static const double TICK_SIZE = 0.01;
static const double BID_MIN = 50.00;
static const double BID_MAX = 99.99;
static const double ASK_MIN = 100.01;
static const double ASK_MAX = 150.00;

static double gen_price(mt19937& rng, bool is_buy) {
    if (is_buy) {
        long tick = uniform_int_distribution<long>(
            static_cast<long>(BID_MIN * 100),
            static_cast<long>(BID_MAX * 100)
        )(rng);
        return tick * TICK_SIZE;
    } else {
        long tick = uniform_int_distribution<long>(
            static_cast<long>(ASK_MIN * 100),
            static_cast<long>(ASK_MAX * 100)
        )(rng);
        return tick * TICK_SIZE;
    }
}

void generate_workload(vector<Operation>& ops) {
    mt19937 rng(42);
    uniform_real_distribution<double> op_type_dist(0.0, 1.0);
    uniform_real_distribution<double> side_dist(0.0, 1.0);
    uniform_int_distribution<long> qty_dist(1, 100);

    vector<LiveOrder> live_orders;
    live_orders.reserve(1000000);

    for (uint64_t i = 0; i < 1000000; ++i) {
        double r = op_type_dist(rng);
        if (r < 0.7) {
            Side side = (side_dist(rng) < 0.5) ? Side::Buy : Side::Sell;
            double price = gen_price(rng, side == Side::Buy);
            long qty = qty_dist(rng);
            ops.push_back({Operation::ADD, i, side, price, qty});
            live_orders.push_back({i, side, price, qty});
        } else if (r < 0.9) {
            if (!live_orders.empty()) {
                uniform_int_distribution<size_t> dist(0, live_orders.size() - 1);
                size_t idx = dist(rng);
                LiveOrder lo = live_orders[idx];
                ops.push_back({Operation::CANCEL, lo.id, lo.side, lo.price, lo.qty});
                live_orders[idx] = live_orders.back();
                live_orders.pop_back();
            } else {
                ops.push_back({Operation::BBO_Q, 0, Side::Buy, 0, 0});
            }
        } else {
            ops.push_back({Operation::BBO_Q, 0, Side::Buy, 0, 0});
        }
    }
}

bool bbo_equal(const BBO& a, const BBO& b) {
    if (a.has_bid != b.has_bid || a.has_ask != b.has_ask) {
        cout << "has_bid/has_ask mismatch: (" << a.has_bid << "," << a.has_ask << ") vs (" << b.has_bid << "," << b.has_ask << ")" << endl;
        return false;
    }
    if (a.has_bid) {
        if (fabs(a.bid_price - b.bid_price) > 1e-9 || a.bid_qty != b.bid_qty) {
            cout << "BBO mismatch: bid " << a.bid_price << " x " << a.bid_qty
                 << " vs " << b.bid_price << " x " << b.bid_qty << endl;
            return false;
        }
    }
    if (a.has_ask) {
        if (fabs(a.ask_price - b.ask_price) > 1e-9 || a.ask_qty != b.ask_qty) {
            cout << "BBO mismatch: ask " << a.ask_price << " x " << a.ask_qty
                 << " vs " << b.ask_price << " x " << b.ask_qty << endl;
            return false;
        }
    }
    return true;
}

bool levels_equal(const vector<Level>& a, const vector<Level>& b) {
    if (a.size() != b.size()) {
        cout << "Level count mismatch: " << a.size() << " vs " << b.size() << endl;
        return false;
    }
    for (size_t i = 0; i < a.size(); i++) {
        if (fabs(a[i].price - b[i].price) > 1e-9 || a[i].qty != b[i].qty) {
            cout << "Level mismatch at index " << i << ": (" << a[i].price << ", " << a[i].qty
                 << ") vs (" << b[i].price << ", " << b[i].qty << ")" << endl;
            return false;
        }
    }
    return true;
}

void run_verify() {
    cout << "Running correctness test..." << endl;
    vector<Operation> ops;
    generate_workload(ops);

    unique_ptr<OrderBook> book_a(new OrderBookA());
    unique_ptr<OrderBook> book_b(new OrderBookB());
    unique_ptr<OrderBook> book_c(new OrderBookC());

    bool pass_a = true, pass_b = true, pass_c = true;
    int depth = 10;

    for (size_t idx = 0; idx < ops.size(); ++idx) {
        const auto& op = ops[idx];
        if (op.type == Operation::ADD) {
            book_a->add_order(op.id, op.side, op.price, op.qty);
            book_b->add_order(op.id, op.side, op.price, op.qty);
            book_c->add_order(op.id, op.side, op.price, op.qty);
        } else if (op.type == Operation::CANCEL) {
            book_a->cancel_order(op.id);
            book_b->cancel_order(op.id);
            book_c->cancel_order(op.id);
        } else {
            BBO bbo_a = book_a->get_bbo();
            BBO bbo_b = book_b->get_bbo();
            BBO bbo_c = book_c->get_bbo();

            if (pass_a && pass_b && !bbo_equal(bbo_a, bbo_b)) {
                pass_a = pass_b = false;
                cout << "Mismatch at operation " << idx << ": A vs B BBO mismatch" << endl;
            }
            if (pass_a && pass_c && !bbo_equal(bbo_a, bbo_c)) {
                pass_a = pass_c = false;
                cout << "Mismatch at operation " << idx << ": A vs C BBO mismatch" << endl;
            }
            if (pass_b && pass_c && !bbo_equal(bbo_b, bbo_c)) {
                pass_b = pass_c = false;
                cout << "Mismatch at operation " << idx << ": B vs C BBO mismatch" << endl;
            }
            if (!pass_a && !pass_b && !pass_c) break;
        }

        // Compare levels
        auto bids_a = book_a->get_bids(depth);
        auto bids_b = book_b->get_bids(depth);
        auto bids_c = book_c->get_bids(depth);

        if (pass_a && pass_b && !levels_equal(bids_a, bids_b)) {
            pass_a = pass_b = false;
            cout << "Mismatch at operation " << idx << ": BIDs mismatch" << endl;
        }
        if (pass_b && pass_c && !levels_equal(bids_b, bids_c)) {
            pass_b = pass_c = false;
            cout << "Mismatch at operation " << idx << ": B vs C BIDs mismatch" << endl;
        }
        if (!pass_a && !pass_b && !pass_c) break;

        auto asks_a = book_a->get_asks(depth);
        auto asks_b = book_b->get_asks(depth);
        auto asks_c = book_c->get_asks(depth);

        if (pass_a && pass_b && !levels_equal(asks_a, asks_b)) {
            pass_a = pass_b = false;
            cout << "Mismatch at operation " << idx << ": ASKs mismatch" << endl;
        }
        if (pass_a && pass_c && !levels_equal(asks_a, asks_c)) {
            pass_a = pass_c = false;
            cout << "Mismatch at operation " << idx << ": A vs C ASKs mismatch" << endl;
        }
        if (!pass_a && !pass_b && !pass_c) break;
    }

    cout << "Implementation A: " << (pass_a ? "PASS" : "FAIL") << endl;
    cout << "Implementation B: " << (pass_b ? "PASS" : "FAIL") << endl;
    cout << "Implementation C: " << (pass_c ? "PASS" : "FAIL") << endl;
    if (pass_a && pass_b && pass_c) {
        cout << "All implementations produce identical results." << endl;
    }
}

unique_ptr<OrderBook> create_order_book(char impl) {
    switch (impl) {
        case 'A': return unique_ptr<OrderBook>(new OrderBookA());
        case 'B': return unique_ptr<OrderBook>(new OrderBookB());
        case 'C': return unique_ptr<OrderBook>(new OrderBookC());
        default:
            cerr << "Unknown implementation: " << impl << endl;
            exit(EXIT_FAILURE);
    }
}

struct BenchResult {
    long total_ms;
    long add_ns;
    long cancel_ns;
    long bbo_ns;
    long add_count;
    long cancel_count;
    long bbo_count;
};

BenchResult run_single_bench(char impl, const vector<Operation>& ops) {
    auto book = create_order_book(impl);

    long add_total_ns = 0;
    long cancel_total_ns = 0;
    long bbo_total_ns = 0;
    long add_count = 0;
    long cancel_count = 0;
    long bbo_count = 0;

    auto total_start = chrono::steady_clock::now();

    for (size_t i = 0; i < ops.size(); ++i) {
        const auto& op = ops[i];
        if (op.type == Operation::ADD) {
            auto t0 = chrono::steady_clock::now();
            book->add_order(op.id, op.side, op.price, op.qty);
            auto t1 = chrono::steady_clock::now();
            add_total_ns += (t1 - t0).count();
            add_count++;
        } else if (op.type == Operation::CANCEL) {
            auto t0 = chrono::steady_clock::now();
            book->cancel_order(op.id);
            auto t1 = chrono::steady_clock::now();
            cancel_total_ns += (t1 - t0).count();
            cancel_count++;
        } else {
            auto t0 = chrono::steady_clock::now();
            book->get_bbo();
            auto t1 = chrono::steady_clock::now();
            bbo_total_ns += (t1 - t0).count();
            bbo_count++;

            // Every 100 ops: get bids and asks (not measured separately for now)
            // The get_bids/get_asks is already included in the BBO_Q timing
        }
    }

    auto total_end = chrono::steady_clock::now();
    long total_ms = (total_end - total_start).count() / 1000000;

    return {
        total_ms,
        add_count > 0 ? add_total_ns / add_count : 0,
        cancel_count > 0 ? cancel_total_ns / cancel_count : 0,
        bbo_count > 0 ? bbo_total_ns / bbo_count : 0,
        add_count,
        cancel_count,
        bbo_count
    };
}

long median(vector<long>& vals) {
    sort(vals.begin(), vals.end());
    return vals[vals.size() / 2];
}

void run_bench(char impl, int runs) {
    cout << "Running benchmark for implementation " << impl << "..." << endl;
    vector<Operation> ops;
    generate_workload(ops);

    cout << "Operation counts:" << endl;
    long add_count = 0, cancel_count = 0, bbo_count = 0;
    for (const auto& op : ops) {
        if (op.type == Operation::ADD) add_count++;
        else if (op.type == Operation::CANCEL) cancel_count++;
        else bbo_count++;
    }
    cout << "  Add: " << add_count << endl;
    cout << "  Cancel: " << cancel_count << endl;
    cout << "  BBO: " << bbo_count << endl;

    vector<long> totals, add_ns, cancel_ns, bbo_ns;

    for (int run = 0; run < runs; ++run) {
        BenchResult result = run_single_bench(impl, ops);
        totals.push_back(result.total_ms);
        add_ns.push_back(result.add_ns);
        cancel_ns.push_back(result.cancel_ns);
        bbo_ns.push_back(result.bbo_ns);
    }

    cout << "Total time (median of " << runs << " runs): " << median(totals) << " ms" << endl;
    cout << "ns / add (median): " << median(add_ns) << endl;
    cout << "ns / cancel (median): " << median(cancel_ns) << endl;
    cout << "ns / BBO (median): " << median(bbo_ns) << endl;

    // Print final BBO for checksum / anti-optimization
    auto book = create_order_book(impl);
    // Replay to get final state
    vector<LiveOrder> live;
    live.reserve(1000000);
    for (const auto& op : ops) {
        if (op.type == Operation::ADD) {
            book->add_order(op.id, op.side, op.price, op.qty);
            live.push_back({op.id, op.side, op.price, op.qty});
        } else if (op.type == Operation::CANCEL) {
            book->cancel_order(op.id);
        }
    }

    BBO bbo = book->get_bbo();
    cout << "Final BBO:" << endl;
    if (bbo.has_bid) {
        cout << "Bid: " << fixed << setprecision(2) << bbo.bid_price << " x " << bbo.bid_qty << endl;
    } else {
        cout << "Bid: none" << endl;
    }
    if (bbo.has_ask) {
        cout << "Ask: " << fixed << setprecision(2) << bbo.ask_price << " x " << bbo.ask_qty << endl;
    } else {
        cout << "Ask: none" << endl;
    }

    uint64_t checksum = 0;
    if (bbo.has_bid) {
        checksum += static_cast<uint64_t>(bbo.bid_price * 100) + bbo.bid_qty;
    }
    if (bbo.has_ask) {
        checksum += static_cast<uint64_t>(bbo.ask_price * 100) + bbo.ask_qty;
    }
    cout << "Checksum: " << checksum << endl;
}

int main(int argc, char* argv[]) {
    char impl = 'A';
    bool verify = false;
    int runs = 5;

    for (int i = 1; i < argc; ++i) {
        string arg = argv[i];
        if (arg == "--impl" && i + 1 < argc) {
            impl = argv[++i][0];
        } else if (arg == "--verify") {
            verify = true;
        } else if (arg == "--runs" && i + 1 < argc) {
            runs = stoi(argv[++i]);
        } else if (arg == "--help") {
            cout << "Usage: ./bench [--impl A|B|C] [--verify] [--runs N]\n";
            return 0;
        }
    }

    if (verify) {
        run_verify();
        return 0;
    }

    run_bench(impl, runs);
    return 0;
}

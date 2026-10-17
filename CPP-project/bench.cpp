#include "order_book.hpp"
#include "order_book_map.hpp"
#include "order_book_vector.hpp"
#include "order_book_direct.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <memory>
#include <random>
#include <string>
#include <vector>

using namespace std;
using namespace std::chrono;

struct Op {
    enum class Type { Add, Cancel, BBO };
    Type type;
    uint64_t id{0};
    Side side{Side::Buy};
    double price{0.0};
    long qty{0};
    bool check_depth{false};
};

struct BenchmarkResult {
    double total_ms{0.0};
    double ns_per_add{0.0};
    double ns_per_cancel{0.0};
    double ns_per_bbo{0.0};
    BBO final_bbo{};
    uint64_t checksum{0};
};

vector<Op> generate_workload(size_t total_ops = 1000000) {
    vector<Op> ops;
    ops.reserve(total_ops);

    mt19937 rng(42);
    uniform_int_distribution<int> dist_type(1, 100);
    uniform_int_distribution<int> dist_side(0, 1);
    uniform_int_distribution<long> dist_qty(1, 100);
    uniform_int_distribution<uint32_t> dist_bid_tick(5000, 9999);
    uniform_int_distribution<uint32_t> dist_ask_tick(10001, 15000);

    vector<uint64_t> live_ids;
    live_ids.reserve(total_ops);

    uint64_t next_id = 1;

    for (size_t i = 1; i <= total_ops; ++i) {
        int r = dist_type(rng);
        bool check_depth = (i % 100 == 0);

        if (r <= 70 || live_ids.empty()) {
            Side side = (dist_side(rng) == 0) ? Side::Buy : Side::Sell;
            uint32_t tick = (side == Side::Buy) ? dist_bid_tick(rng) : dist_ask_tick(rng);
            double price = tick_to_price(tick);
            long qty = dist_qty(rng);

            uint64_t id = next_id++;
            live_ids.push_back(id);

            ops.push_back(Op{Op::Type::Add, id, side, price, qty, check_depth});
        } else if (r <= 90) {
            uniform_int_distribution<size_t> dist_idx(0, live_ids.size() - 1);
            size_t idx = dist_idx(rng);
            uint64_t id = live_ids[idx];

            live_ids[idx] = live_ids.back();
            live_ids.pop_back();

            ops.push_back(Op{Op::Type::Cancel, id, Side::Buy, 0.0, 0, check_depth});
        } else {
            ops.push_back(Op{Op::Type::BBO, 0, Side::Buy, 0.0, 0, check_depth});
        }
    }

    return ops;
}

unique_ptr<OrderBook> create_book(char impl) {
    if (impl == 'A') {
        return make_unique<OrderBookMap>();
    }
    if (impl == 'B') {
        return make_unique<OrderBookVector>();
    }
    if (impl == 'C') {
        return make_unique<OrderBookDirect>();
    }
    return nullptr;
}

BenchmarkResult run_single_benchmark(char impl, const vector<Op>& ops) {
    auto book = create_book(impl);

    uint64_t checksum = 0;
    nanoseconds total_add_ns{0};
    nanoseconds total_cancel_ns{0};
    nanoseconds total_bbo_ns{0};

    size_t count_add = 0;
    size_t count_cancel = 0;
    size_t count_bbo = 0;

    auto t_start = steady_clock::now();

    for (const auto& op : ops) {
        if (op.type == Op::Type::Add) {
            auto t0 = steady_clock::now();
            book->add_order(op.id, op.side, op.price, op.qty);
            auto t1 = steady_clock::now();
            total_add_ns += (t1 - t0);
            ++count_add;
        } else if (op.type == Op::Type::Cancel) {
            auto t0 = steady_clock::now();
            bool ok = book->cancel_order(op.id);
            auto t1 = steady_clock::now();
            total_cancel_ns += (t1 - t0);
            checksum += ok ? 1 : 0;
            ++count_cancel;
        } else {
            auto t0 = steady_clock::now();
            BBO bbo = book->get_bbo();
            auto t1 = steady_clock::now();
            total_bbo_ns += (t1 - t0);
            checksum += (bbo.has_bid ? static_cast<uint64_t>(bbo.bid_qty) : 0) +
                        (bbo.has_ask ? static_cast<uint64_t>(bbo.ask_qty) : 0);
            ++count_bbo;
        }

        if (op.check_depth) {
            for (const auto& l : book->get_bids(10)) {
                checksum += static_cast<uint64_t>(l.qty) + 1;
            }
            for (const auto& l : book->get_asks(10)) {
                checksum += static_cast<uint64_t>(l.qty) + 1;
            }
        }
    }

    auto t_end = steady_clock::now();

    BBO final_bbo = book->get_bbo();
    if (final_bbo.has_bid) {
        checksum += price_to_tick(final_bbo.bid_price) + static_cast<uint64_t>(final_bbo.bid_qty);
    }
    if (final_bbo.has_ask) {
        checksum += price_to_tick(final_bbo.ask_price) + static_cast<uint64_t>(final_bbo.ask_qty);
    }

    BenchmarkResult res;
    res.total_ms = duration<double, milli>(t_end - t_start).count();
    res.ns_per_add = (count_add > 0) ? (static_cast<double>(total_add_ns.count()) / count_add) : 0.0;
    res.ns_per_cancel = (count_cancel > 0) ? (static_cast<double>(total_cancel_ns.count()) / count_cancel) : 0.0;
    res.ns_per_bbo = (count_bbo > 0) ? (static_cast<double>(total_bbo_ns.count()) / count_bbo) : 0.0;
    res.final_bbo = final_bbo;
    res.checksum = checksum;

    return res;
}

void run_benchmark_suite(char impl) {
    const vector<Op> ops = generate_workload(1000000);

    vector<BenchmarkResult> runs;
    runs.reserve(5);

    for (int i = 0; i < 5; ++i) {
        runs.push_back(run_single_benchmark(impl, ops));
    }

    sort(runs.begin(), runs.end(), [](const BenchmarkResult& a, const BenchmarkResult& b) {
        return a.total_ms < b.total_ms;
    });

    const BenchmarkResult& median = runs[2];

    cout << fixed << setprecision(2);
    cout << "=== Implementation " << impl << " Benchmark (5 runs, median reported) ===" << "\n";
    cout << "Total execution time: " << median.total_ms << " ms" << "\n";
    cout << "Average ns / add:     " << median.ns_per_add << " ns" << "\n";
    cout << "Average ns / cancel:  " << median.ns_per_cancel << " ns" << "\n";
    cout << "Average ns / get_bbo: " << median.ns_per_bbo << " ns" << "\n";
    cout << "Final BBO:" << "\n";
    if (median.final_bbo.has_bid) {
        cout << "  Bid: " << median.final_bbo.bid_price << " x " << median.final_bbo.bid_qty << "\n";
    } else {
        cout << "  Bid: None" << "\n";
    }
    if (median.final_bbo.has_ask) {
        cout << "  Ask: " << median.final_bbo.ask_price << " x " << median.final_bbo.ask_qty << "\n";
    } else {
        cout << "  Ask: None" << "\n";
    }
    cout << "Checksum: " << median.checksum << "\n";
}

static bool compare_bbo(const BBO& a, const BBO& b) {
    if (a.has_bid != b.has_bid || a.has_ask != b.has_ask) {
        return false;
    }
    if (a.has_bid && (fabs(a.bid_price - b.bid_price) > 1e-6 || a.bid_qty != b.bid_qty)) {
        return false;
    }
    if (a.has_ask && (fabs(a.ask_price - b.ask_price) > 1e-6 || a.ask_qty != b.ask_qty)) {
        return false;
    }
    return true;
}

static bool compare_levels(const vector<Level>& a, const vector<Level>& b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (size_t i = 0; i < a.size(); ++i) {
        if (fabs(a[i].price - b[i].price) > 1e-6 || a[i].qty != b[i].qty) {
            return false;
        }
    }
    return true;
}

bool run_correctness_verification() {
    cout << "Running correctness test..." << "\n";

    const vector<Op> ops = generate_workload(1000000);

    OrderBookMap book_a;
    OrderBookVector book_b;
    OrderBookDirect book_c;

    for (size_t i = 0; i < ops.size(); ++i) {
        const auto& op = ops[i];

        if (op.type == Op::Type::Add) {
            book_a.add_order(op.id, op.side, op.price, op.qty);
            book_b.add_order(op.id, op.side, op.price, op.qty);
            book_c.add_order(op.id, op.side, op.price, op.qty);
        } else if (op.type == Op::Type::Cancel) {
            bool ca = book_a.cancel_order(op.id);
            bool cb = book_b.cancel_order(op.id);
            bool cc = book_c.cancel_order(op.id);
            if (ca != cb || cb != cc) {
                cerr << "Mismatch on cancel_order id " << op.id << " at op " << i << "\n";
                return false;
            }
        }

        BBO bbo_a = book_a.get_bbo();
        BBO bbo_b = book_b.get_bbo();
        BBO bbo_c = book_c.get_bbo();

        if (!compare_bbo(bbo_a, bbo_b) || !compare_bbo(bbo_b, bbo_c)) {
            cerr << "Mismatch on BBO at op " << i << "\n";
            return false;
        }

        if (op.check_depth) {
            auto bids_a = book_a.get_bids(10), bids_b = book_b.get_bids(10), bids_c = book_c.get_bids(10);
            if (!compare_levels(bids_a, bids_b) || !compare_levels(bids_b, bids_c)) {
                cerr << "Mismatch on bids depth at op " << i << "\n";
                return false;
            }

            auto asks_a = book_a.get_asks(10), asks_b = book_b.get_asks(10), asks_c = book_c.get_asks(10);
            if (!compare_levels(asks_a, asks_b) || !compare_levels(asks_b, asks_c)) {
                cerr << "Mismatch on asks depth at op " << i << "\n";
                return false;
            }
        }
    }

    cout << "Implementation A: PASS" << "\n";
    cout << "Implementation B: PASS" << "\n";
    cout << "Implementation C: PASS" << "\n";
    cout << "All implementations produce identical results." << "\n";

    return true;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        cerr << "Usage: " << argv[0] << " --impl [A|B|C] | --verify" << "\n";
        return 1;
    }

    string arg = argv[1];

    if (arg == "--verify") {
        bool ok = run_correctness_verification();
        return ok ? 0 : 1;
    }

    if (arg == "--impl") {
        if (argc < 3) {
            cerr << "Error: --impl requires A, B, or C" << "\n";
            return 1;
        }
        char impl = argv[2][0];
        if (impl != 'A' && impl != 'B' && impl != 'C') {
            cerr << "Error: Unknown implementation: " << argv[2] << " (use A, B, or C)" << "\n";
            return 1;
        }
        run_benchmark_suite(impl);
        return 0;
    }

    cerr << "Unknown argument: " << arg << "\n";
    return 1;
}

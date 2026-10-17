#include <iostream>
#include <vector>
#include <string>
#include <chrono>
#include <random>
#include <algorithm>
#include <iomanip>
#include <cstdint>

#include "common.hpp"
#include "order_book_map.hpp"
#include "order_book_vector.hpp"
#include "order_book_array.hpp"

// Operation types in the benchmark workload
enum class OpType {
    Add,
    Cancel,
    GetBBO
};

struct Operation {
    OpType type;
    uint64_t id;
    Side side;
    double price;
    long qty;
    bool query_depth; // If true, also query get_bids(10) and get_asks(10)
};

// Generates 1,000,000 deterministic operations
std::vector<Operation> generate_workload(int count = 1000000, unsigned int seed = 42) {
    std::mt19937 rng(seed);
    std::vector<Operation> ops;
    ops.reserve(count);

    std::vector<uint64_t> live_orders;
    live_orders.reserve(count);

    uint64_t next_id = 1;

    for (int i = 0; i < count; ++i) {
        bool query_depth = (i % 100 == 0);
        int roll = rng() % 100;

        // Distribution: ~70% Add, ~20% Cancel, ~10% BBO
        if (roll < 70 || live_orders.empty()) {
            // Add order
            uint64_t id = next_id++;
            Side side = (rng() % 2 == 0) ? Side::Buy : Side::Sell;
            double price = 0.0;
            if (side == Side::Buy) {
                // Bid price range: [50.00, 99.99]
                int tick = Config::MIN_BID_CENTS + (rng() % (Config::MAX_BID_CENTS - Config::MIN_BID_CENTS + 1));
                price = to_double(tick);
            } else {
                // Ask price range: [100.01, 150.00]
                int tick = Config::MIN_ASK_CENTS + (rng() % (Config::MAX_ASK_CENTS - Config::MIN_ASK_CENTS + 1));
                price = to_double(tick);
            }
            long qty = 1 + (rng() % 100);

            live_orders.push_back(id);
            ops.push_back(Operation{OpType::Add, id, side, price, qty, query_depth});
        } else if (roll < 90) {
            // Cancel order: always pick a currently live order
            size_t idx = rng() % live_orders.size();
            uint64_t id = live_orders[idx];
            // Swap with back for O(1) removal
            live_orders[idx] = live_orders.back();
            live_orders.pop_back();

            ops.push_back(Operation{OpType::Cancel, id, Side::Buy, 0.0, 0, query_depth});
        } else {
            // BBO query
            ops.push_back(Operation{OpType::GetBBO, 0, Side::Buy, 0.0, 0, query_depth});
        }
    }

    return ops;
}

// Result structure for one benchmark run
struct BenchmarkResult {
    double total_time_ms;
    double ns_per_add;
    double ns_per_cancel;
    double ns_per_bbo;
    BBO final_bbo;
    uint64_t checksum;
};

// Generic benchmark runner for any OrderBook implementation
template <typename BookType>
BenchmarkResult run_single_benchmark(const std::vector<Operation>& ops) {
    BookType book;

    uint64_t total_add_ns = 0;
    uint64_t total_cancel_ns = 0;
    uint64_t total_bbo_ns = 0;

    size_t count_add = 0;
    size_t count_cancel = 0;
    size_t count_bbo = 0;

    uint64_t checksum = 0;

    auto t_total_start = std::chrono::steady_clock::now();

    for (const auto& op : ops) {
        if (op.type == OpType::Add) {
            auto t0 = std::chrono::steady_clock::now();
            book.add_order(op.id, op.side, op.price, op.qty);
            auto t1 = std::chrono::steady_clock::now();
            total_add_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
            count_add++;
        } else if (op.type == OpType::Cancel) {
            auto t0 = std::chrono::steady_clock::now();
            bool res = book.cancel_order(op.id);
            auto t1 = std::chrono::steady_clock::now();
            total_cancel_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
            count_cancel++;
            checksum += (res ? 1 : 0);
        } else {
            auto t0 = std::chrono::steady_clock::now();
            BBO bbo = book.get_bbo();
            auto t1 = std::chrono::steady_clock::now();
            total_bbo_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
            count_bbo++;

            checksum += (bbo.has_bid ? static_cast<uint64_t>(bbo.bid_qty) : 0);
            checksum += (bbo.has_ask ? static_cast<uint64_t>(bbo.ask_qty) : 0);
        }

        if (op.query_depth) {
            auto bids = book.get_bids(10);
            auto asks = book.get_asks(10);
            for (const auto& l : bids) {
                checksum += l.qty + static_cast<uint64_t>(l.price * 100);
            }
            for (const auto& l : asks) {
                checksum += l.qty + static_cast<uint64_t>(l.price * 100);
            }
        }
    }

    auto t_total_end = std::chrono::steady_clock::now();
    double total_ms = std::chrono::duration<double, std::milli>(t_total_end - t_total_start).count();

    BenchmarkResult res;
    res.total_time_ms = total_ms;
    res.ns_per_add = (count_add > 0) ? (static_cast<double>(total_add_ns) / count_add) : 0.0;
    res.ns_per_cancel = (count_cancel > 0) ? (static_cast<double>(total_cancel_ns) / count_cancel) : 0.0;
    res.ns_per_bbo = (count_bbo > 0) ? (static_cast<double>(total_bbo_ns) / count_bbo) : 0.0;
    res.final_bbo = book.get_bbo();
    res.checksum = checksum;

    return res;
}

template <typename BookType>
void run_benchmarks_for_impl(const std::string& name) {
    std::cout << "========================================================\n";
    std::cout << " Running Benchmark: " << name << "\n";
    std::cout << " Workload: 1,000,000 operations (70% Add, 20% Cancel, 10% BBO)\n";
    std::cout << " Runs: 5 iterations (Reporting median result)\n";
    std::cout << "========================================================\n";

    auto ops = generate_workload(1000000, 42);

    std::vector<BenchmarkResult> results;
    results.reserve(5);

    for (int run = 1; run <= 5; ++run) {
        BenchmarkResult r = run_single_benchmark<BookType>(ops);
        results.push_back(r);

        std::cout << "Run " << run << ": "
                  << std::fixed << std::setprecision(2) << r.total_time_ms << " ms | "
                  << "Add: " << std::setprecision(1) << r.ns_per_add << " ns | "
                  << "Cancel: " << r.ns_per_cancel << " ns | "
                  << "BBO: " << r.ns_per_bbo << " ns\n";
    }

    // Sort to compute median by total_time_ms
    std::vector<double> times_ms, adds_ns, cancels_ns, bbos_ns;
    for (const auto& r : results) {
        times_ms.push_back(r.total_time_ms);
        adds_ns.push_back(r.ns_per_add);
        cancels_ns.push_back(r.ns_per_cancel);
        bbos_ns.push_back(r.ns_per_bbo);
    }
    std::sort(times_ms.begin(), times_ms.end());
    std::sort(adds_ns.begin(), adds_ns.end());
    std::sort(cancels_ns.begin(), cancels_ns.end());
    std::sort(bbos_ns.begin(), bbos_ns.end());

    std::cout << "\n--------------------------------------------------------\n";
    std::cout << " MEDIAN RESULTS (" << name << "):\n";
    std::cout << " Total Time:       " << std::fixed << std::setprecision(2) << times_ms[2] << " ms\n";
    std::cout << " Avg ns / add:     " << std::setprecision(1) << adds_ns[2] << " ns\n";
    std::cout << " Avg ns / cancel:  " << std::setprecision(1) << cancels_ns[2] << " ns\n";
    std::cout << " Avg ns / BBO:     " << std::setprecision(1) << bbos_ns[2] << " ns\n";
    std::cout << "--------------------------------------------------------\n";
    std::cout << "Final BBO:\n";
    if (results[0].final_bbo.has_bid) {
        std::cout << "  Bid: " << std::fixed << std::setprecision(2)
                  << results[0].final_bbo.bid_price << " x " << results[0].final_bbo.bid_qty << "\n";
    } else {
        std::cout << "  Bid: None\n";
    }
    if (results[0].final_bbo.has_ask) {
        std::cout << "  Ask: " << std::fixed << std::setprecision(2)
                  << results[0].final_bbo.ask_price << " x " << results[0].final_bbo.ask_qty << "\n";
    } else {
        std::cout << "  Ask: None\n";
    }
    std::cout << "\nChecksum: " << results[0].checksum << "\n";
    std::cout << "========================================================\n\n";
}

// Correctness verification
bool run_verification() {
    std::cout << "Running correctness test...\n\n";

    OrderBookMap bookA;
    OrderBookVector bookB;
    OrderBookArray bookC;

    // Test 1: Empty book
    BBO bboA = bookA.get_bbo();
    BBO bboB = bookB.get_bbo();
    BBO bboC = bookC.get_bbo();
    if (bboA.has_bid || bboA.has_ask || bboB.has_bid || bboB.has_ask || bboC.has_bid || bboC.has_ask) {
        std::cerr << "FAILED: Empty book has_bid or has_ask returned true\n";
        return false;
    }

    // Test 2: Unknown order cancel
    if (bookA.cancel_order(999999) != false ||
        bookB.cancel_order(999999) != false ||
        bookC.cancel_order(999999) != false) {
        std::cerr << "FAILED: Cancel of unknown order did not return false\n";
        return false;
    }

    // Test 3: Insufficient depth
    bookA.add_order(101, Side::Buy, 90.00, 10);
    bookA.add_order(102, Side::Buy, 89.00, 20);
    bookA.add_order(103, Side::Buy, 88.00, 30);

    bookB.add_order(101, Side::Buy, 90.00, 10);
    bookB.add_order(102, Side::Buy, 89.00, 20);
    bookB.add_order(103, Side::Buy, 88.00, 30);

    bookC.add_order(101, Side::Buy, 90.00, 10);
    bookC.add_order(102, Side::Buy, 89.00, 20);
    bookC.add_order(103, Side::Buy, 88.00, 30);

    auto bidsA_depth = bookA.get_bids(10);
    auto bidsB_depth = bookB.get_bids(10);
    auto bidsC_depth = bookC.get_bids(10);
    if (bidsA_depth.size() != 3 || bidsB_depth.size() != 3 || bidsC_depth.size() != 3) {
        std::cerr << "FAILED: Insufficient depth check expected 3 levels\n";
        return false;
    }

    // Test 4: Cancel already cancelled order
    bookA.cancel_order(101);
    bookB.cancel_order(101);
    bookC.cancel_order(101);
    if (bookA.cancel_order(101) != false ||
        bookB.cancel_order(101) != false ||
        bookC.cancel_order(101) != false) {
        std::cerr << "FAILED: Cancel of already cancelled order did not return false\n";
        return false;
    }

    // Reset books for full workload test
    OrderBookMap full_bookA;
    OrderBookVector full_bookB;
    OrderBookArray full_bookC;

    // Generate 100,000 operations for rigorous verification
    auto ops = generate_workload(100000, 42);

    for (size_t i = 0; i < ops.size(); ++i) {
        const auto& op = ops[i];

        if (op.type == OpType::Add) {
            full_bookA.add_order(op.id, op.side, op.price, op.qty);
            full_bookB.add_order(op.id, op.side, op.price, op.qty);
            full_bookC.add_order(op.id, op.side, op.price, op.qty);
        } else if (op.type == OpType::Cancel) {
            bool ca = full_bookA.cancel_order(op.id);
            bool cb = full_bookB.cancel_order(op.id);
            bool cc = full_bookC.cancel_order(op.id);
            if (ca != cb || cb != cc) {
                std::cerr << "FAILED: Cancel return mismatch at op " << i << "\n";
                return false;
            }
        } else {
            BBO a = full_bookA.get_bbo();
            BBO b = full_bookB.get_bbo();
            BBO c = full_bookC.get_bbo();

            if (a.has_bid != b.has_bid || a.has_bid != c.has_bid ||
                a.bid_price != b.bid_price || a.bid_price != c.bid_price ||
                a.bid_qty != b.bid_qty || a.bid_qty != c.bid_qty ||
                a.has_ask != b.has_ask || a.has_ask != c.has_ask ||
                a.ask_price != b.ask_price || a.ask_price != c.ask_price ||
                a.ask_qty != b.ask_qty || a.ask_qty != c.ask_qty) {
                std::cerr << "FAILED: BBO mismatch at op " << i << "\n";
                return false;
            }
        }

        if (op.query_depth) {
            auto bidsA = full_bookA.get_bids(10);
            auto bidsB = full_bookB.get_bids(10);
            auto bidsC = full_bookC.get_bids(10);
            if (bidsA.size() != bidsB.size() || bidsA.size() != bidsC.size()) {
                std::cerr << "FAILED: Bids depth size mismatch at op " << i << "\n";
                return false;
            }
            for (size_t k = 0; k < bidsA.size(); ++k) {
                if (bidsA[k].price != bidsB[k].price || bidsA[k].price != bidsC[k].price ||
                    bidsA[k].qty != bidsB[k].qty || bidsA[k].qty != bidsC[k].qty) {
                    std::cerr << "FAILED: Bid level content mismatch at op " << i << "\n";
                    return false;
                }
            }

            auto asksA = full_bookA.get_asks(10);
            auto asksB = full_bookB.get_asks(10);
            auto asksC = full_bookC.get_asks(10);
            if (asksA.size() != asksB.size() || asksA.size() != asksC.size()) {
                std::cerr << "FAILED: Asks depth size mismatch at op " << i << "\n";
                return false;
            }
            for (size_t k = 0; k < asksA.size(); ++k) {
                if (asksA[k].price != asksB[k].price || asksA[k].price != asksC[k].price ||
                    asksA[k].qty != asksB[k].qty || asksA[k].qty != asksC[k].qty) {
                    std::cerr << "FAILED: Ask level content mismatch at op " << i << "\n";
                    return false;
                }
            }
        }
    }

    std::cout << "Implementation A: PASS\n";
    std::cout << "Implementation B: PASS\n";
    std::cout << "Implementation C: PASS\n\n";
    std::cout << "All implementations produce identical results.\n";
    return true;
}

void print_usage(const char* prog) {
    std::cout << "Usage:\n";
    std::cout << "  " << prog << " --verify      Run correctness test across all implementations\n";
    std::cout << "  " << prog << " --impl A       Benchmark Implementation A (std::map / Red-Black Tree)\n";
    std::cout << "  " << prog << " --impl B       Benchmark Implementation B (std::vector / Sorted Array)\n";
    std::cout << "  " << prog << " --impl C       Benchmark Implementation C (Direct Price Array / Custom)\n";
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    std::string arg = argv[1];

    if (arg == "--verify") {
        bool ok = run_verification();
        return ok ? 0 : 1;
    } else if (arg == "--impl") {
        if (argc < 3) {
            std::cerr << "Error: Missing implementation choice (A, B, or C)\n";
            print_usage(argv[0]);
            return 1;
        }
        std::string choice = argv[2];
        if (choice == "A" || choice == "a") {
            run_benchmarks_for_impl<OrderBookMap>("Implementation A (std::map / Red-Black Tree)");
        } else if (choice == "B" || choice == "b") {
            run_benchmarks_for_impl<OrderBookVector>("Implementation B (std::vector / Sorted Array)");
        } else if (choice == "C" || choice == "c") {
            run_benchmarks_for_impl<OrderBookArray>("Implementation C (Direct Price-Indexed Array)");
        } else {
            std::cerr << "Error: Unknown implementation '" << choice << "'. Choose A, B, or C.\n";
            return 1;
        }
    } else {
        std::cerr << "Error: Unrecognized option '" << arg << "'\n";
        print_usage(argv[0]);
        return 1;
    }

    return 0;
}

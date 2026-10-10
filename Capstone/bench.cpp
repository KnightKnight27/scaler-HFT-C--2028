#include "order_book.hpp"
#include "order_book_map.hpp"
#include "order_book_vector.hpp"
#include "order_book_direct.hpp"

#include <iostream>
#include <vector>
#include <random>
#include <chrono>
#include <memory>
#include <string>
#include <algorithm>
#include <iomanip>
#include <cassert>

enum class OpType {
    Add,
    Cancel,
    GetBBO,
    GetDepth
};

struct Operation {
    OpType type;
    uint64_t id{0};
    Side side{Side::Buy};
    double price{0.0};
    long qty{0};
};

// Workload generator with fixed seed 42
std::vector<Operation> generate_workload(size_t num_ops = 1'000'000) {
    std::mt19937 rng(42);
    std::uniform_int_distribution<int> op_dist(0, 99);       // 70% Add, 20% Cancel, 10% BBO
    std::uniform_int_distribution<int> side_dist(0, 1);       // 50% Buy, 50% Sell
    std::uniform_int_distribution<long> qty_dist(1, 100);     // Qty 1..100
    std::uniform_int_distribution<int> bid_tick_dist(5000, 9999);   // [50.00, 99.99]
    std::uniform_int_distribution<int> ask_tick_dist(10001, 15000); // [100.01, 150.00]

    std::vector<Operation> ops;
    ops.reserve(num_ops + num_ops / 100);

    std::vector<uint64_t> live_orders;
    live_orders.reserve(num_ops);

    uint64_t next_id = 1;

    for (size_t i = 0; i < num_ops; ++i) {
        int roll = op_dist(rng);

        if (roll < 70 || live_orders.empty()) { // 70% Add (or if no live orders to cancel)
            Operation op;
            op.type = OpType::Add;
            op.id = next_id++;
            op.side = (side_dist(rng) == 0) ? Side::Buy : Side::Sell;
            if (op.side == Side::Buy) {
                op.price = bid_tick_dist(rng) / 100.0;
            } else {
                op.price = ask_tick_dist(rng) / 100.0;
            }
            op.qty = qty_dist(rng);

            live_orders.push_back(op.id);
            ops.push_back(op);
        } else if (roll < 90) { // 20% Cancel
            Operation op;
            op.type = OpType::Cancel;
            size_t idx = rng() % live_orders.size();
            op.id = live_orders[idx];

            // Fast removal from live pool
            live_orders[idx] = live_orders.back();
            live_orders.pop_back();

            ops.push_back(op);
        } else { // 10% GetBBO
            Operation op;
            op.type = OpType::GetBBO;
            ops.push_back(op);
        }

        // Every 100 operations, call get_bids(10) and get_asks(10)
        if ((i + 1) % 100 == 0) {
            Operation op;
            op.type = OpType::GetDepth;
            ops.push_back(op);
        }
    }

    return ops;
}

// Verification function to ensure all 3 implementations produce identical results
bool run_verification(const std::vector<Operation>& ops) {
    std::cout << "Running correctness test...\n\n";

    OrderBookMap bookA;
    OrderBookVector bookB;
    OrderBookDirect bookC;

    uint64_t verify_step = 0;

    for (const auto& op : ops) {
        verify_step++;
        switch (op.type) {
            case OpType::Add: {
                bookA.add_order(op.id, op.side, op.price, op.qty);
                bookB.add_order(op.id, op.side, op.price, op.qty);
                bookC.add_order(op.id, op.side, op.price, op.qty);
                break;
            }
            case OpType::Cancel: {
                bool ca = bookA.cancel_order(op.id);
                bool cb = bookB.cancel_order(op.id);
                bool cc = bookC.cancel_order(op.id);
                if (ca != cb || cb != cc) {
                    std::cerr << "Verification failed at op " << verify_step 
                              << ": cancel_order return mismatch (" << ca << ", " << cb << ", " << cc << ")\n";
                    return false;
                }
                break;
            }
            case OpType::GetBBO: {
                BBO bboA = bookA.get_bbo();
                BBO bboB = bookB.get_bbo();
                BBO bboC = bookC.get_bbo();

                if (!(bboA == bboB) || !(bboB == bboC)) {
                    std::cerr << "Verification failed at op " << verify_step << ": BBO mismatch!\n";
                    std::cerr << "Impl A: Bid(" << bboA.has_bid << ", " << bboA.bid_price << ", " << bboA.bid_qty 
                              << ") Ask(" << bboA.has_ask << ", " << bboA.ask_price << ", " << bboA.ask_qty << ")\n";
                    std::cerr << "Impl B: Bid(" << bboB.has_bid << ", " << bboB.bid_price << ", " << bboB.bid_qty 
                              << ") Ask(" << bboB.has_ask << ", " << bboB.ask_price << ", " << bboB.ask_qty << ")\n";
                    std::cerr << "Impl C: Bid(" << bboC.has_bid << ", " << bboC.bid_price << ", " << bboC.bid_qty 
                              << ") Ask(" << bboC.has_ask << ", " << bboC.ask_price << ", " << bboC.ask_qty << ")\n";
                    return false;
                }
                break;
            }
            case OpType::GetDepth: {
                auto bidsA = bookA.get_bids(10);
                auto bidsB = bookB.get_bids(10);
                auto bidsC = bookC.get_bids(10);
                if (bidsA != bidsB || bidsB != bidsC) {
                    std::cerr << "Verification failed at op " << verify_step << ": get_bids(10) mismatch!\n";
                    return false;
                }

                auto asksA = bookA.get_asks(10);
                auto asksB = bookB.get_asks(10);
                auto asksC = bookC.get_asks(10);
                if (asksA != asksB || asksB != asksC) {
                    std::cerr << "Verification failed at op " << verify_step << ": get_asks(10) mismatch!\n";
                    return false;
                }
                break;
            }
        }
    }

    std::cout << "Implementation A: PASS\n";
    std::cout << "Implementation B: PASS\n";
    std::cout << "Implementation C: PASS\n\n";
    std::cout << "All implementations produce identical results.\n";
    return true;
}

struct TrialStats {
    double total_time_ms{0.0};
    double ns_per_add{0.0};
    double ns_per_cancel{0.0};
    double ns_per_bbo{0.0};
    BBO final_bbo;
    uint64_t checksum{0};
};

std::unique_ptr<IOrderBook> create_book(char impl) {
    if (impl == 'A') return std::make_unique<OrderBookMap>();
    if (impl == 'B') return std::make_unique<OrderBookVector>();
    if (impl == 'C') return std::make_unique<OrderBookDirect>();
    throw std::runtime_error("Invalid implementation selection");
}

TrialStats run_single_trial(char impl, const std::vector<Operation>& ops) {
    auto book = create_book(impl);

    uint64_t add_count = 0;
    uint64_t cancel_count = 0;
    uint64_t bbo_count = 0;

    int64_t add_time_ns = 0;
    int64_t cancel_time_ns = 0;
    int64_t bbo_time_ns = 0;

    uint64_t checksum = 0;

    auto trial_start = std::chrono::steady_clock::now();

    for (const auto& op : ops) {
        switch (op.type) {
            case OpType::Add: {
                auto t0 = std::chrono::steady_clock::now();
                book->add_order(op.id, op.side, op.price, op.qty);
                auto t1 = std::chrono::steady_clock::now();
                add_time_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
                add_count++;
                break;
            }
            case OpType::Cancel: {
                auto t0 = std::chrono::steady_clock::now();
                bool res = book->cancel_order(op.id);
                auto t1 = std::chrono::steady_clock::now();
                cancel_time_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
                cancel_count++;
                checksum += (res ? 1 : 0);
                break;
            }
            case OpType::GetBBO: {
                auto t0 = std::chrono::steady_clock::now();
                BBO bbo = book->get_bbo();
                auto t1 = std::chrono::steady_clock::now();
                bbo_time_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
                bbo_count++;
                if (bbo.has_bid) checksum += static_cast<uint64_t>(bbo.bid_qty);
                if (bbo.has_ask) checksum += static_cast<uint64_t>(bbo.ask_qty);
                break;
            }
            case OpType::GetDepth: {
                auto bids = book->get_bids(10);
                auto asks = book->get_asks(10);
                checksum += bids.size() + asks.size();
                break;
            }
        }
    }

    auto trial_end = std::chrono::steady_clock::now();
    double total_ms = std::chrono::duration<double, std::milli>(trial_end - trial_start).count();

    TrialStats stats;
    stats.total_time_ms = total_ms;
    stats.ns_per_add = (add_count > 0) ? (static_cast<double>(add_time_ns) / add_count) : 0.0;
    stats.ns_per_cancel = (cancel_count > 0) ? (static_cast<double>(cancel_time_ns) / cancel_count) : 0.0;
    stats.ns_per_bbo = (bbo_count > 0) ? (static_cast<double>(bbo_time_ns) / bbo_count) : 0.0;
    stats.final_bbo = book->get_bbo();
    stats.checksum = checksum;

    return stats;
}

void run_benchmark_suite(char impl, const std::vector<Operation>& ops, int trials = 5) {
    std::cout << "=========================================================\n";
    std::cout << "Benchmarking Implementation " << impl << " (" << trials << " trials, 1,000,000 ops)\n";
    std::cout << "=========================================================\n";

    std::vector<TrialStats> all_stats;
    all_stats.reserve(trials);

    for (int t = 1; t <= trials; ++t) {
        TrialStats s = run_single_trial(impl, ops);
        all_stats.push_back(s);
        std::cout << "  Trial " << t << ": Total Time = " << std::fixed << std::setprecision(2) 
                  << s.total_time_ms << " ms | add: " << std::setprecision(1) << s.ns_per_add 
                  << " ns | cancel: " << s.ns_per_cancel << " ns | BBO: " << s.ns_per_bbo << " ns\n";
    }

    // Extract median based on total execution time
    std::sort(all_stats.begin(), all_stats.end(), [](const TrialStats& a, const TrialStats& b) {
        return a.total_time_ms < b.total_time_ms;
    });

    const TrialStats& median = all_stats[trials / 2];

    std::cout << "\n---------------------------------------------------------\n";
    std::cout << "MEDIAN RESULTS FOR IMPLEMENTATION " << impl << ":\n";
    std::cout << "  Total execution time : " << std::fixed << std::setprecision(2) << median.total_time_ms << " ms\n";
    std::cout << "  Average ns / add     : " << std::setprecision(2) << median.ns_per_add << " ns\n";
    std::cout << "  Average ns / cancel  : " << std::setprecision(2) << median.ns_per_cancel << " ns\n";
    std::cout << "  Average ns / BBO     : " << std::setprecision(2) << median.ns_per_bbo << " ns\n";
    std::cout << "\nPreventing Compiler Optimization:\n";
    std::cout << "  Final BBO:\n";
    if (median.final_bbo.has_bid) {
        std::cout << "    Bid: " << std::fixed << std::setprecision(2) << median.final_bbo.bid_price 
                  << " x " << median.final_bbo.bid_qty << "\n";
    } else {
        std::cout << "    Bid: None\n";
    }
    if (median.final_bbo.has_ask) {
        std::cout << "    Ask: " << std::fixed << std::setprecision(2) << median.final_bbo.ask_price 
                  << " x " << median.final_bbo.ask_qty << "\n";
    } else {
        std::cout << "    Ask: None\n";
    }
    std::cout << "  Checksum: " << median.checksum << "\n";
    std::cout << "=========================================================\n\n";
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cout << "Usage:\n";
        std::cout << "  " << argv[0] << " --verify\n";
        std::cout << "  " << argv[0] << " --impl A\n";
        std::cout << "  " << argv[0] << " --impl B\n";
        std::cout << "  " << argv[0] << " --impl C\n";
        std::cout << "  " << argv[0] << " --all\n";
        return 1;
    }

    std::string arg = argv[1];

    std::cout << "Generating 1,000,000 operation workload (Seed: 42)...\n";
    auto ops = generate_workload(1'000'000);
    std::cout << "Generated " << ops.size() << " operations.\n\n";

    if (arg == "--verify") {
        bool ok = run_verification(ops);
        return ok ? 0 : 1;
    } else if (arg == "--impl") {
        if (argc < 3) {
            std::cerr << "Error: --impl requires A, B, or C\n";
            return 1;
        }
        char impl = argv[2][0];
        if (impl != 'A' && impl != 'B' && impl != 'C') {
            std::cerr << "Error: Unknown implementation '" << argv[2] << "'. Use A, B, or C.\n";
            return 1;
        }
        run_benchmark_suite(impl, ops, 5);
    } else if (arg == "--all") {
        run_benchmark_suite('A', ops, 5);
        run_benchmark_suite('B', ops, 5);
        run_benchmark_suite('C', ops, 5);
    } else {
        std::cerr << "Unknown argument: " << arg << "\n";
        return 1;
    }

    return 0;
}

#include "OrderBookA.hpp"
#include "OrderBookB.hpp"
#include "OrderBookC.hpp"
#include "Workload.hpp"

#include <iostream>
#include <iomanip>
#include <chrono>
#include <vector>
#include <memory>
#include <string>
#include <algorithm>
#include <cassert>

using Clock = std::chrono::steady_clock;

struct TimingStats {
    double total_time_ms{0.0};
    double ns_per_add{0.0};
    double ns_per_cancel{0.0};
    double ns_per_bbo{0.0};
    uint64_t checksum{0};
    BBO final_bbo{};
};

// Execute workload and measure timings
TimingStats run_single_pass(OrderBookBase& book, const std::vector<Operation>& workload) {
    uint64_t checksum = 0;

    uint64_t total_add_ns = 0;
    uint64_t total_cancel_ns = 0;
    uint64_t total_bbo_ns = 0;

    size_t count_add = 0;
    size_t count_cancel = 0;
    size_t count_bbo = 0;

    auto start_total = Clock::now();

    for (const auto& op : workload) {
        switch (op.type) {
            case OpType::Add: {
                auto t0 = Clock::now();
                book.add_order(op.order_id, op.side, op.price, op.qty);
                auto t1 = Clock::now();
                total_add_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
                ++count_add;
                checksum ^= (op.order_id * 1000003ULL) + static_cast<uint64_t>(op.qty);
                break;
            }
            case OpType::Cancel: {
                auto t0 = Clock::now();
                bool res = book.cancel_order(op.order_id);
                auto t1 = Clock::now();
                total_cancel_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
                ++count_cancel;
                checksum ^= (op.order_id * 524287ULL) + (res ? 1ULL : 0ULL);
                break;
            }
            case OpType::GetBBO: {
                auto t0 = Clock::now();
                BBO bbo = book.get_bbo();
                auto t1 = Clock::now();
                total_bbo_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
                ++count_bbo;
                if (bbo.has_bid) {
                    checksum ^= price_to_cents(bbo.bid_price) * 31ULL + bbo.bid_qty;
                }
                if (bbo.has_ask) {
                    checksum ^= price_to_cents(bbo.ask_price) * 73ULL + bbo.ask_qty;
                }
                break;
            }
            case OpType::GetDepth: {
                auto bids = book.get_bids(10);
                auto asks = book.get_asks(10);
                for (const auto& lvl : bids) {
                    checksum ^= price_to_cents(lvl.price) * 17ULL + lvl.qty;
                }
                for (const auto& lvl : asks) {
                    checksum ^= price_to_cents(lvl.price) * 23ULL + lvl.qty;
                }
                break;
            }
        }
    }

    auto end_total = Clock::now();
    double total_ms = std::chrono::duration<double, std::milli>(end_total - start_total).count();

    BBO final_bbo = book.get_bbo();
    if (final_bbo.has_bid) {
        checksum += price_to_cents(final_bbo.bid_price) + final_bbo.bid_qty;
    }
    if (final_bbo.has_ask) {
        checksum += price_to_cents(final_bbo.ask_price) + final_bbo.ask_qty;
    }

    TimingStats stats;
    stats.total_time_ms = total_ms;
    stats.ns_per_add = (count_add > 0) ? (static_cast<double>(total_add_ns) / count_add) : 0.0;
    stats.ns_per_cancel = (count_cancel > 0) ? (static_cast<double>(total_cancel_ns) / count_cancel) : 0.0;
    stats.ns_per_bbo = (count_bbo > 0) ? (static_cast<double>(total_bbo_ns) / count_bbo) : 0.0;
    stats.checksum = checksum;
    stats.final_bbo = final_bbo;

    return stats;
}

std::unique_ptr<OrderBookBase> create_book(char impl) {
    switch (impl) {
        case 'A':
        case 'a':
            return std::make_unique<OrderBookA>();
        case 'B':
        case 'b':
            return std::make_unique<OrderBookB>();
        case 'C':
        case 'c':
            return std::make_unique<OrderBookC>();
        default:
            return nullptr;
    }
}

void print_bbo(const BBO& bbo) {
    std::cout << "Final BBO:\n";
    if (bbo.has_bid) {
        std::cout << "  Bid: " << std::fixed << std::setprecision(2) << bbo.bid_price << " x " << bbo.bid_qty << "\n";
    } else {
        std::cout << "  Bid: [EMPTY]\n";
    }
    if (bbo.has_ask) {
        std::cout << "  Ask: " << std::fixed << std::setprecision(2) << bbo.ask_price << " x " << bbo.ask_qty << "\n";
    } else {
        std::cout << "  Ask: [EMPTY]\n";
    }
}

bool run_correctness_verification(size_t ops_count = 100000) {
    std::cout << "=========================================================\n";
    std::cout << " Running Order Book Correctness Verification (" << ops_count << " ops)\n";
    std::cout << "=========================================================\n";

    auto workload = WorkloadGenerator::generate(ops_count, 42);

    OrderBookA bookA;
    OrderBookB bookB;
    OrderBookC bookC;

    bool passA = true;
    bool passB = true;
    bool passC = true;

    for (size_t i = 0; i < workload.size(); ++i) {
        const auto& op = workload[i];
        switch (op.type) {
            case OpType::Add: {
                bookA.add_order(op.order_id, op.side, op.price, op.qty);
                bookB.add_order(op.order_id, op.side, op.price, op.qty);
                bookC.add_order(op.order_id, op.side, op.price, op.qty);
                break;
            }
            case OpType::Cancel: {
                bool rA = bookA.cancel_order(op.order_id);
                bool rB = bookB.cancel_order(op.order_id);
                bool rC = bookC.cancel_order(op.order_id);
                if (rA != rB) { passB = false; }
                if (rA != rC) { passC = false; }
                break;
            }
            case OpType::GetBBO: {
                BBO bboA = bookA.get_bbo();
                BBO bboB = bookB.get_bbo();
                BBO bboC = bookC.get_bbo();
                if (!(bboA == bboB)) { passB = false; }
                if (!(bboA == bboC)) { passC = false; }
                break;
            }
            case OpType::GetDepth: {
                auto bidsA = bookA.get_bids(10);
                auto bidsB = bookB.get_bids(10);
                auto bidsC = bookC.get_bids(10);

                auto asksA = bookA.get_asks(10);
                auto asksB = bookB.get_asks(10);
                auto asksC = bookC.get_asks(10);

                if (bidsA != bidsB || asksA != asksB) { passB = false; }
                if (bidsA != bidsC || asksA != asksC) { passC = false; }
                break;
            }
        }

        if (!passB || !passC) {
            std::cerr << "Mismatch detected at operation " << i << "!\n";
            break;
        }
    }

    // Final state cross-check
    BBO finalA = bookA.get_bbo();
    BBO finalB = bookB.get_bbo();
    BBO finalC = bookC.get_bbo();

    if (!(finalA == finalB)) passB = false;
    if (!(finalA == finalC)) passC = false;

    auto all_bidsA = bookA.get_bids(100);
    auto all_bidsB = bookB.get_bids(100);
    auto all_bidsC = bookC.get_bids(100);
    if (all_bidsA != all_bidsB) passB = false;
    if (all_bidsA != all_bidsC) passC = false;

    std::cout << "\nRunning correctness test...\n\n";
    std::cout << "Implementation A: " << (passA ? "PASS" : "FAIL") << "\n";
    std::cout << "Implementation B: " << (passB ? "PASS" : "FAIL") << "\n";
    std::cout << "Implementation C: " << (passC ? "PASS" : "FAIL") << "\n\n";

    if (passA && passB && passC) {
        std::cout << "All implementations produce identical results.\n";
        print_bbo(finalA);
        return true;
    } else {
        std::cerr << "Verification failed: Implementations did not agree!\n";
        return false;
    }
}

void benchmark_implementation(char impl, int repetitions = 5, size_t total_ops = 1000000) {
    auto book_dummy = create_book(impl);
    if (!book_dummy) {
        std::cerr << "Unknown implementation: " << impl << "\n";
        return;
    }

    std::cout << "=========================================================\n";
    std::cout << " Benchmarking: " << book_dummy->get_name() << "\n";
    std::cout << " Total Operations: " << total_ops << " | Repetitions: " << repetitions << "\n";
    std::cout << "=========================================================\n";

    std::cout << "Generating repeatable workload (seed=42)... " << std::flush;
    auto workload = WorkloadGenerator::generate(total_ops, 42);
    std::cout << "Done (" << workload.size() << " operations with depth checks).\n\n";

    std::vector<TimingStats> runs;
    runs.reserve(repetitions);

    for (int r = 0; r < repetitions; ++r) {
        auto book = create_book(impl);
        std::cout << "  Run " << (r + 1) << "/" << repetitions << "... " << std::flush;
        TimingStats stats = run_single_pass(*book, workload);
        runs.push_back(stats);
        std::cout << std::fixed << std::setprecision(2)
                  << stats.total_time_ms << " ms | "
                  << stats.ns_per_add << " ns/add | "
                  << stats.ns_per_cancel << " ns/cancel | "
                  << stats.ns_per_bbo << " ns/BBO\n";
    }

    // Sort by total execution time to extract median
    std::sort(runs.begin(), runs.end(), [](const TimingStats& a, const TimingStats& b) {
        return a.total_time_ms < b.total_time_ms;
    });

    const auto& median = runs[repetitions / 2];

    std::cout << "\n---------------------------------------------------------\n";
    std::cout << " Median Result for " << book_dummy->get_name() << ":\n";
    std::cout << "---------------------------------------------------------\n";
    std::cout << "  Total time (ms):  " << std::fixed << std::setprecision(2) << median.total_time_ms << " ms\n";
    std::cout << "  ns / add:         " << std::fixed << std::setprecision(2) << median.ns_per_add << " ns\n";
    std::cout << "  ns / cancel:      " << std::fixed << std::setprecision(2) << median.ns_per_cancel << " ns\n";
    std::cout << "  ns / BBO:         " << std::fixed << std::setprecision(2) << median.ns_per_bbo << " ns\n";
    std::cout << "  Checksum:         " << median.checksum << "\n";
    print_bbo(median.final_bbo);
    std::cout << "---------------------------------------------------------\n\n";
}

int main(int argc, char* argv[]) {
    char impl_to_run = 'A';
    bool verify_mode = false;
    bool run_all = false;
    int repetitions = 5;
    size_t total_ops = 1000000;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--verify") {
            verify_mode = true;
        } else if (arg == "--impl" && i + 1 < argc) {
            std::string val = argv[++i];
            if (val == "all" || val == "ALL") {
                run_all = true;
            } else if (val.size() == 1) {
                impl_to_run = val[0];
            }
        } else if (arg == "--runs" && i + 1 < argc) {
            repetitions = std::stoi(argv[++i]);
        } else if (arg == "--ops" && i + 1 < argc) {
            total_ops = std::stoull(argv[++i]);
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: ./bench [options]\n"
                      << "  --verify             Run correctness verification across all 3 implementations\n"
                      << "  --impl <A|B|C|all>   Select implementation to benchmark (default: A)\n"
                      << "  --runs <N>           Number of repetitions to compute median (default: 5)\n"
                      << "  --ops <N>            Total operations to run (default: 1000000)\n";
            return 0;
        }
    }

    if (verify_mode) {
        bool ok = run_correctness_verification(total_ops < 1000000 ? total_ops : 100000);
        return ok ? 0 : 1;
    }

    if (run_all) {
        benchmark_implementation('A', repetitions, total_ops);
        benchmark_implementation('B', repetitions, total_ops);
        benchmark_implementation('C', repetitions, total_ops);
    } else {
        benchmark_implementation(impl_to_run, repetitions, total_ops);
    }

    return 0;
}

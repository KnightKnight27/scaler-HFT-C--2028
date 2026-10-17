#include <iostream>
#include <vector>
#include <string>
#include <chrono>
#include <random>
#include <algorithm>
#include <iomanip>
#include <memory>
#include <cstdint>
#include <sys/resource.h>

#include "order.h"
#include "impl_a/orderbook_a.h"
#include "impl_b/orderbook_b.h"
#include "impl_c/orderbook_c.h"

enum class OpType : uint8_t {
    Add,
    Cancel,
    GetBBO,
    GetDepth
};

struct Operation {
    OpType type;
    Side side;
    double price;
    long qty;
    uint64_t id;
};

// Workload generation according to assignment rules
std::vector<Operation> generate_workload(size_t num_ops, uint32_t seed) {
    std::mt19937 rng(seed);
    std::uniform_int_distribution<int> op_dist(1, 100);
    std::uniform_int_distribution<int> side_dist(0, 1);
    std::uniform_int_distribution<long> qty_dist(1, 100);
    std::uniform_int_distribution<long> bid_tick_dist(5000, 9999);
    std::uniform_int_distribution<long> ask_tick_dist(10001, 15000);

    std::vector<Operation> ops;
    ops.reserve(num_ops + num_ops / 100);

    std::vector<uint64_t> live_orders;
    uint64_t next_id = 1;

    for (size_t i = 1; i <= num_ops; ++i) {
        int r = op_dist(rng);
        if (r <= 70 || live_orders.empty()) {
            // 70% Add
            Side side = (side_dist(rng) == 0) ? Side::Buy : Side::Sell;
            long tick = (side == Side::Buy) ? bid_tick_dist(rng) : ask_tick_dist(rng);
            double price = tick / 100.0;
            long qty = qty_dist(rng);
            uint64_t id = next_id++;
            live_orders.push_back(id);
            ops.push_back({OpType::Add, side, price, qty, id});
        } else if (r <= 90) {
            // 20% Cancel (must select an order that is currently live)
            std::uniform_int_distribution<size_t> idx_dist(0, live_orders.size() - 1);
            size_t idx = idx_dist(rng);
            uint64_t id = live_orders[idx];
            live_orders[idx] = live_orders.back();
            live_orders.pop_back();
            ops.push_back({OpType::Cancel, Side::Buy, 0.0, 0, id});
        } else {
            // 10% BBO query
            ops.push_back({OpType::GetBBO, Side::Buy, 0.0, 0, 0});
        }

        // Depth query every 100 operations
        if (i % 100 == 0) {
            ops.push_back({OpType::GetDepth, Side::Buy, 0.0, 0, 0});
        }
    }

    return ops;
}

struct RunMetrics {
    double total_time_ms;
    double ns_per_add;
    double ns_per_cancel;
    double ns_per_bbo;
    long page_faults;
    long max_rss_kb;
    BBO final_bbo;
    uint64_t checksum;
};

RunMetrics execute_run(IOrderBook* book, const std::vector<Operation>& ops) {
    using clock = std::chrono::steady_clock;

    struct rusage ru_before, ru_after;
    getrusage(RUSAGE_SELF, &ru_before);

    uint64_t total_add_ns = 0;
    uint64_t total_cancel_ns = 0;
    uint64_t total_bbo_ns = 0;

    size_t add_count = 0;
    size_t cancel_count = 0;
    size_t bbo_count = 0;

    uint64_t checksum = 0;

    auto t_start = clock::now();

    for (const auto& op : ops) {
        switch (op.type) {
            case OpType::Add: {
                auto s = clock::now();
                book->add_order(op.id, op.side, op.price, op.qty);
                auto e = clock::now();
                total_add_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(e - s).count();
                add_count++;
                break;
            }
            case OpType::Cancel: {
                auto s = clock::now();
                book->cancel_order(op.id);
                auto e = clock::now();
                total_cancel_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(e - s).count();
                cancel_count++;
                break;
            }
            case OpType::GetBBO: {
                auto s = clock::now();
                BBO bbo = book->get_bbo();
                auto e = clock::now();
                total_bbo_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(e - s).count();
                bbo_count++;
                if (bbo.has_bid) checksum += bbo.bid_level.qty + static_cast<uint64_t>(bbo.bid_level.price * 100);
                if (bbo.has_ask) checksum += bbo.ask_level.qty + static_cast<uint64_t>(bbo.ask_level.price * 100);
                break;
            }
            case OpType::GetDepth: {
                auto bids = book->get_bids(10);
                auto asks = book->get_asks(10);
                for (const auto& lvl : bids) checksum += lvl.qty + static_cast<uint64_t>(lvl.price * 100);
                for (const auto& lvl : asks) checksum += lvl.qty + static_cast<uint64_t>(lvl.price * 100);
                break;
            }
        }
    }

    auto t_end = clock::now();
    getrusage(RUSAGE_SELF, &ru_after);

    RunMetrics m;
    m.total_time_ms = std::chrono::duration<double, std::milli>(t_end - t_start).count();
    m.ns_per_add = (add_count > 0) ? static_cast<double>(total_add_ns) / add_count : 0.0;
    m.ns_per_cancel = (cancel_count > 0) ? static_cast<double>(total_cancel_ns) / cancel_count : 0.0;
    m.ns_per_bbo = (bbo_count > 0) ? static_cast<double>(total_bbo_ns) / bbo_count : 0.0;
    long faults_before = ru_before.ru_minflt + ru_before.ru_majflt;
    long faults_after = ru_after.ru_minflt + ru_after.ru_majflt;
    m.page_faults = faults_after - faults_before;
    m.max_rss_kb = ru_after.ru_maxrss;
    m.final_bbo = book->get_bbo();
    m.checksum = checksum;

    return m;
}

std::unique_ptr<IOrderBook> create_book(const std::string& impl) {
    if (impl == "A" || impl == "a") return std::make_unique<OrderBookA>();
    if (impl == "B" || impl == "b") return std::make_unique<OrderBookB>();
    if (impl == "C" || impl == "c") return std::make_unique<OrderBookC>();
    return nullptr;
}

void run_verification(size_t num_ops, uint32_t seed) {
    std::cout << "Running correctness test...\n";

    auto ops = generate_workload(num_ops, seed);
    OrderBookA book_a;
    OrderBookB book_b;
    OrderBookC book_c;

    bool match_a = true;
    bool match_b = true;
    bool match_c = true;

    for (size_t i = 0; i < ops.size(); ++i) {
        const auto& op = ops[i];
        if (op.type == OpType::Add) {
            book_a.add_order(op.id, op.side, op.price, op.qty);
            book_b.add_order(op.id, op.side, op.price, op.qty);
            book_c.add_order(op.id, op.side, op.price, op.qty);
        } else if (op.type == OpType::Cancel) {
            bool ca = book_a.cancel_order(op.id);
            bool cb = book_b.cancel_order(op.id);
            bool cc = book_c.cancel_order(op.id);
            if (ca != cb || ca != cc) {
                std::cerr << "Cancel mismatch at op " << i << " (id: " << op.id << "): "
                          << "A=" << ca << ", B=" << cb << ", C=" << cc << "\n";
                match_b = match_c = false;
                break;
            }
        }

        // Check BBO
        BBO bbo_a = book_a.get_bbo();
        BBO bbo_b = book_b.get_bbo();
        BBO bbo_c = book_c.get_bbo();

        auto bbo_equals = [](const BBO& x, const BBO& y) {
            if (x.has_bid != y.has_bid || x.has_ask != y.has_ask) return false;
            if (x.has_bid && (x.bid_level.price != y.bid_level.price || x.bid_level.qty != y.bid_level.qty)) return false;
            if (x.has_ask && (x.ask_level.price != y.ask_level.price || x.ask_level.qty != y.ask_level.qty)) return false;
            return true;
        };

        if (!bbo_equals(bbo_a, bbo_b)) {
            std::cerr << "BBO mismatch between A and B at op " << i << "\n";
            match_b = false;
            break;
        }
        if (!bbo_equals(bbo_a, bbo_c)) {
            std::cerr << "BBO mismatch between A and C at op " << i << "\n";
            match_c = false;
            break;
        }

        // Depth check every 100 ops
        if (op.type == OpType::GetDepth || (i % 100 == 0)) {
            auto bids_a = book_a.get_bids(10);
            auto bids_b = book_b.get_bids(10);
            auto bids_c = book_c.get_bids(10);

            auto asks_a = book_a.get_asks(10);
            auto asks_b = book_b.get_asks(10);
            auto asks_c = book_c.get_asks(10);

            auto depth_equals = [](const std::vector<Level>& v1, const std::vector<Level>& v2) {
                if (v1.size() != v2.size()) return false;
                for (size_t k = 0; k < v1.size(); ++k) {
                    if (v1[k].price != v2[k].price || v1[k].qty != v2[k].qty) return false;
                }
                return true;
            };

            if (!depth_equals(bids_a, bids_b) || !depth_equals(asks_a, asks_b)) {
                std::cerr << "Depth mismatch between A and B at op " << i << "\n";
                match_b = false;
                break;
            }
            if (!depth_equals(bids_a, bids_c) || !depth_equals(asks_a, asks_c)) {
                std::cerr << "Depth mismatch between A and C at op " << i << "\n";
                match_c = false;
                break;
            }
        }
    }

    std::cout << "\n";
    std::cout << "Implementation A: " << (match_a ? "PASS" : "FAIL") << "\n";
    std::cout << "Implementation B: " << (match_b ? "PASS" : "FAIL") << "\n";
    std::cout << "Implementation C: " << (match_c ? "PASS" : "FAIL") << "\n\n";

    if (match_a && match_b && match_c) {
        std::cout << "All implementations produce identical results.\n";
    } else {
        std::cout << "Verification FAILED: implementations differed.\n";
    }
}

void print_help(const char* prog) {
    std::cout << "Usage: " << prog << " [OPTIONS]\n\n"
              << "Options:\n"
              << "  --impl <A|B|C>    Select implementation to benchmark\n"
              << "  --verify          Run correctness check across all implementations\n"
              << "  --ops <N>         Number of operations (default: 1,000,000)\n"
              << "  --runs <N>        Number of benchmark iterations (default: 5)\n"
              << "  --seed <S>        Random seed for workload (default: 42)\n"
              << "  -h, --help        Show this help message\n\n"
              << "Examples:\n"
              << "  " << prog << " --impl A\n"
              << "  " << prog << " --impl B\n"
              << "  " << prog << " --impl C\n"
              << "  " << prog << " --verify\n";
}

int main(int argc, char* argv[]) {
    std::string impl = "";
    bool verify_mode = false;
    size_t num_ops = 1000000;
    size_t num_runs = 5;
    uint32_t seed = 42;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--impl" && i + 1 < argc) {
            impl = argv[++i];
        } else if (arg == "--verify") {
            verify_mode = true;
        } else if (arg == "--ops" && i + 1 < argc) {
            num_ops = std::stoull(argv[++i]);
        } else if (arg == "--runs" && i + 1 < argc) {
            num_runs = std::stoull(argv[++i]);
        } else if (arg == "--seed" && i + 1 < argc) {
            seed = static_cast<uint32_t>(std::stoul(argv[++i]));
        } else if (arg == "-h" || arg == "--help") {
            print_help(argv[0]);
            return 0;
        } else {
            std::cerr << "Unknown or incomplete argument: " << arg << "\n";
            print_help(argv[0]);
            return 1;
        }
    }

    if (verify_mode) {
        // Verification run
        size_t verify_ops = (num_ops == 1000000) ? 25000 : num_ops;
        run_verification(verify_ops, seed);
        return 0;
    }

    if (impl.empty()) {
        std::cerr << "Error: Must specify --impl <A|B|C> or --verify\n\n";
        print_help(argv[0]);
        return 1;
    }

    char impl_upper = static_cast<char>(std::toupper(impl[0]));
    if (impl_upper != 'A' && impl_upper != 'B' && impl_upper != 'C') {
        std::cerr << "Error: Unknown implementation '" << impl << "'. Choose A, B, or C.\n";
        return 1;
    }

    std::cout << "=================================================================\n";
    std::cout << "OrderBook Benchmark: Implementation " << impl_upper << "\n";
    std::cout << "Workload: " << num_ops << " operations | " << num_runs << " runs | Seed: " << seed << "\n";
    std::cout << "=================================================================\n\n";

    std::cout << "Generating workload... " << std::flush;
    auto ops = generate_workload(num_ops, seed);
    std::cout << "Done (" << ops.size() << " operations generated).\n\n";

    std::vector<RunMetrics> runs;
    runs.reserve(num_runs);

    for (size_t r = 1; r <= num_runs; ++r) {
        auto book = create_book(std::string(1, impl_upper));
        RunMetrics m = execute_run(book.get(), ops);
        runs.push_back(m);

        std::cout << "Run " << r << "/" << num_runs << ": "
                  << std::fixed << std::setprecision(2) << m.total_time_ms << " ms | "
                  << "add: " << std::setprecision(1) << m.ns_per_add << " ns | "
                  << "cancel: " << m.ns_per_cancel << " ns | "
                  << "BBO: " << m.ns_per_bbo << " ns | "
                  << "faults: " << m.page_faults << "\n";
    }

    // Compute medians
    auto median_of = [](std::vector<double> vals) -> double {
        std::sort(vals.begin(), vals.end());
        size_t n = vals.size();
        if (n % 2 == 1) return vals[n / 2];
        return (vals[n / 2 - 1] + vals[n / 2]) / 2.0;
    };

    std::vector<double> total_times, add_times, cancel_times, bbo_times, faults;
    for (const auto& r : runs) {
        total_times.push_back(r.total_time_ms);
        add_times.push_back(r.ns_per_add);
        cancel_times.push_back(r.ns_per_cancel);
        bbo_times.push_back(r.ns_per_bbo);
        faults.push_back(static_cast<double>(r.page_faults));
    }

    double med_total = median_of(total_times);
    double med_add = median_of(add_times);
    double med_cancel = median_of(cancel_times);
    double med_bbo = median_of(bbo_times);
    double med_faults = median_of(faults);

    const auto& last = runs.back();

    std::cout << "\n-----------------------------------------------------------------\n";
    std::cout << "MEDIAN RESULTS (" << num_runs << " runs):\n";
    std::cout << "  Total execution time : " << std::fixed << std::setprecision(2) << med_total << " ms\n";
    std::cout << "  Average ns / add     : " << std::fixed << std::setprecision(1) << med_add << " ns\n";
    std::cout << "  Average ns / cancel  : " << med_cancel << " ns\n";
    std::cout << "  Average ns / get_bbo : " << med_bbo << " ns\n";
    std::cout << "  Page faults (median) : " << static_cast<long>(med_faults) << "\n";
    std::cout << "  Max RSS              : " << last.max_rss_kb << " KB\n";
    std::cout << "-----------------------------------------------------------------\n";

    std::cout << "Final BBO:\n";
    if (last.final_bbo.has_bid) {
        std::cout << "  Bid: " << std::fixed << std::setprecision(2) << last.final_bbo.bid_level.price
                  << " x " << last.final_bbo.bid_level.qty << "\n";
    } else {
        std::cout << "  Bid: None\n";
    }
    if (last.final_bbo.has_ask) {
        std::cout << "  Ask: " << std::fixed << std::setprecision(2) << last.final_bbo.ask_level.price
                  << " x " << last.final_bbo.ask_level.qty << "\n";
    } else {
        std::cout << "  Ask: None\n";
    }
    std::cout << "\nChecksum: " << last.checksum << "\n";
    std::cout << "=================================================================\n";

    return 0;
}


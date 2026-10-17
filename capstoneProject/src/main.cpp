#include "OrderBook.hpp"
#include "OrderBookMap.hpp"
#include "OrderBookVector.hpp"
#include "OrderBookArray.hpp"

#include <iostream>
#include <iomanip>
#include <string>
#include <vector>
#include <random>
#include <chrono>
#include <algorithm>
#include <cmath>
#include <cstring>

#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#else
#include <sys/resource.h>
#endif

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#include <x86intrin.h>
#endif

uint64_t get_page_faults() {
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
        return pmc.PageFaultCount;
    }
    return 0;
#else
    struct rusage usage;
    if (getrusage(RUSAGE_SELF, &usage) == 0) {
        return usage.ru_minflt + usage.ru_majflt;
    }
    return 0;
#endif
}

uint64_t get_peak_memory_kb() {
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
        return pmc.PeakWorkingSetSize / 1024;
    }
    return 0;
#else
    struct rusage usage;
    if (getrusage(RUSAGE_SELF, &usage) == 0) {
        return usage.ru_maxrss;
    }
    return 0;
#endif
}

uint64_t read_cpu_cycles() {
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
    return __rdtsc();
#else
    return 0;
#endif
}

enum class OpType { Add, Cancel, GetBBO };

struct BenchmarkOp {
    OpType type;
    uint64_t id;
    Side side;
    double price;
    long qty;
};

// Generates the deterministic 1,000,000 mixed workload
std::vector<BenchmarkOp> generate_workload(int num_operations = 1000000, unsigned int seed = 42) {
    std::mt19937 rng(seed);
    std::uniform_int_distribution<int> op_dist(0, 99); // 70% Add, 20% Cancel, 10% BBO
    std::uniform_int_distribution<int> side_dist(0, 1); // 50% Buy, 50% Sell
    std::uniform_int_distribution<long> qty_dist(1, 100);
    std::uniform_int_distribution<int> bid_dist(5000, 9999);    // 50.00 to 99.99
    std::uniform_int_distribution<int> ask_dist(10001, 15000);  // 100.01 to 150.00

    std::vector<uint64_t> live_order_ids;
    uint64_t next_order_id = 1;

    std::vector<BenchmarkOp> ops;
    ops.reserve(num_operations);

    for (int i = 0; i < num_operations; ++i) {
        int roll = op_dist(rng);
        if (roll < 70 || live_order_ids.empty()) { // Add order
            uint64_t id = next_order_id++;
            Side side = (side_dist(rng) == 0) ? Side::Buy : Side::Sell;
            int tick = (side == Side::Buy) ? bid_dist(rng) : ask_dist(rng);
            double price = tick * 0.01;
            long qty = qty_dist(rng);

            ops.push_back({OpType::Add, id, side, price, qty});
            live_order_ids.push_back(id);
        } else if (roll < 90) { // Cancel order (must always select a currently live order)
            std::uniform_int_distribution<size_t> idx_dist(0, live_order_ids.size() - 1);
            size_t idx = idx_dist(rng);
            uint64_t id_to_cancel = live_order_ids[idx];
            live_order_ids[idx] = live_order_ids.back();
            live_order_ids.pop_back();

            ops.push_back({OpType::Cancel, id_to_cancel, Side::Buy, 0.0, 0});
        } else { // GetBBO
            ops.push_back({OpType::GetBBO, 0, Side::Buy, 0.0, 0});
        }
    }

    return ops;
}

struct RunResult {
    double total_time_ms;
    double ns_per_add;
    double ns_per_cancel;
    double ns_per_bbo;
    uint64_t cycles;
    uint64_t page_faults;
    uint64_t peak_mem_kb;
    BBO final_bbo;
    uint64_t checksum;
};

// Measures separate operation latencies using dedicated batches
template <typename BookType>
void measure_separate_ops(double& ns_per_add, double& ns_per_cancel, double& ns_per_bbo) {
    const int N_OPS = 100000;
    std::mt19937 rng(1337);
    std::uniform_int_distribution<int> side_dist(0, 1);
    std::uniform_int_distribution<long> qty_dist(1, 100);
    std::uniform_int_distribution<int> bid_dist(5000, 9999);
    std::uniform_int_distribution<int> ask_dist(10001, 15000);

    // 1. Measure add_order
    {
        BookType book;
        struct AddSpec { uint64_t id; Side side; double price; long qty; };
        std::vector<AddSpec> adds;
        adds.reserve(N_OPS);
        for (int i = 0; i < N_OPS; ++i) {
            Side s = (side_dist(rng) == 0) ? Side::Buy : Side::Sell;
            double p = ((s == Side::Buy) ? bid_dist(rng) : ask_dist(rng)) * 0.01;
            adds.push_back({(uint64_t)(i + 1), s, p, qty_dist(rng)});
        }

        auto t0 = std::chrono::steady_clock::now();
        for (const auto& a : adds) {
            book.add_order(a.id, a.side, a.price, a.qty);
        }
        auto t1 = std::chrono::steady_clock::now();
        ns_per_add = std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count() / (double)N_OPS;
    }

    // 2. Measure get_bbo on a populated book
    {
        BookType book;
        for (int i = 0; i < 10000; ++i) {
            Side s = (side_dist(rng) == 0) ? Side::Buy : Side::Sell;
            double p = ((s == Side::Buy) ? bid_dist(rng) : ask_dist(rng)) * 0.01;
            book.add_order((uint64_t)(i + 1), s, p, qty_dist(rng));
        }

        volatile bool dummy_has_bid = false;
        auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < N_OPS; ++i) {
            BBO bbo = book.get_bbo();
            dummy_has_bid = bbo.has_bid;
        }
        auto t1 = std::chrono::steady_clock::now();
        (void)dummy_has_bid;
        ns_per_bbo = std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count() / (double)N_OPS;
    }

    // 3. Measure cancel_order
    {
        BookType book;
        std::vector<uint64_t> ids;
        ids.reserve(N_OPS);
        for (int i = 0; i < N_OPS; ++i) {
            uint64_t id = (uint64_t)(i + 1);
            Side s = (side_dist(rng) == 0) ? Side::Buy : Side::Sell;
            double p = ((s == Side::Buy) ? bid_dist(rng) : ask_dist(rng)) * 0.01;
            book.add_order(id, s, p, qty_dist(rng));
            ids.push_back(id);
        }
        std::shuffle(ids.begin(), ids.end(), rng);

        auto t0 = std::chrono::steady_clock::now();
        for (uint64_t id : ids) {
            book.cancel_order(id);
        }
        auto t1 = std::chrono::steady_clock::now();
        ns_per_cancel = std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count() / (double)N_OPS;
    }
}

template <typename BookType>
RunResult run_single_benchmark(const std::vector<BenchmarkOp>& ops) {
    BookType book;
    uint64_t checksum = 0;

    uint64_t pf_start = get_page_faults();
    uint64_t c_start = read_cpu_cycles();
    auto start = std::chrono::steady_clock::now();

    for (size_t i = 0; i < ops.size(); ++i) {
        const auto& op = ops[i];
        switch (op.type) {
            case OpType::Add:
                book.add_order(op.id, op.side, op.price, op.qty);
                break;
            case OpType::Cancel: {
                bool cancelled = book.cancel_order(op.id);
                checksum += (cancelled ? 1 : 0);
                break;
            }
            case OpType::GetBBO: {
                BBO bbo = book.get_bbo();
                if (bbo.has_bid) {
                    checksum = checksum * 31 + (uint64_t)std::lround(bbo.bid_price * 100.0) + bbo.bid_qty;
                }
                if (bbo.has_ask) {
                    checksum = checksum * 31 + (uint64_t)std::lround(bbo.ask_price * 100.0) + bbo.ask_qty;
                }
                break;
            }
        }

        // Every 100 operations, call get_bids(10) and get_asks(10)
        if ((i + 1) % 100 == 0) {
            auto bids = book.get_bids(10);
            for (const auto& lvl : bids) {
                checksum = checksum * 31 + (uint64_t)std::lround(lvl.price * 100.0) + lvl.qty;
            }
            auto asks = book.get_asks(10);
            for (const auto& lvl : asks) {
                checksum = checksum * 31 + (uint64_t)std::lround(lvl.price * 100.0) + lvl.qty;
            }
        }
    }

    auto end = std::chrono::steady_clock::now();
    uint64_t c_end = read_cpu_cycles();
    uint64_t pf_end = get_page_faults();

    double total_time_ms = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count() / 1000.0;
    uint64_t delta_cycles = (c_end >= c_start) ? (c_end - c_start) : 0;
    uint64_t delta_pf = (pf_end >= pf_start) ? (pf_end - pf_start) : 0;
    uint64_t peak_mem = get_peak_memory_kb();
    BBO final_bbo = book.get_bbo();

    double ns_add = 0.0, ns_cancel = 0.0, ns_bbo = 0.0;
    measure_separate_ops<BookType>(ns_add, ns_cancel, ns_bbo);

    return {total_time_ms, ns_add, ns_cancel, ns_bbo, delta_cycles, delta_pf, peak_mem, final_bbo, checksum};
}

template <typename BookType>
void execute_benchmark_typed(const std::string& impl, const std::string& desc, int num_runs, int num_ops) {
    std::cout << "========================================================\n";
    std::cout << "Benchmarking Implementation " << impl << " (" << desc << ")\n";
    std::cout << "Workload: " << num_ops << " operations (70% Add, 20% Cancel, 10% BBO, Depth/100 ops)\n";
    std::cout << "Repetitions: " << num_runs << " runs\n";
    std::cout << "========================================================\n\n";

    std::cout << "Pre-generating workload (seed: 42)... " << std::flush;
    auto ops = generate_workload(num_ops, 42);
    std::cout << "Done.\n\n";

    std::vector<RunResult> results;
    results.reserve(num_runs);

    for (int r = 1; r <= num_runs; ++r) {
        std::cout << "Run " << r << " / " << num_runs << "... " << std::flush;
        auto res = run_single_benchmark<BookType>(ops);
        results.push_back(res);
        std::cout << std::fixed << std::setprecision(2)
                  << res.total_time_ms << " ms | "
                  << "ns/add: " << std::setprecision(1) << res.ns_per_add << " | "
                  << "ns/cancel: " << res.ns_per_cancel << " | "
                  << "ns/BBO: " << res.ns_per_bbo << " | "
                  << "Page faults: " << res.page_faults << "\n";
    }

    std::vector<double> total_times;
    std::vector<double> adds;
    std::vector<double> cancels;
    std::vector<double> bbos;
    std::vector<uint64_t> cycles_list;
    std::vector<uint64_t> pf_list;

    for (const auto& r : results) {
        total_times.push_back(r.total_time_ms);
        adds.push_back(r.ns_per_add);
        cancels.push_back(r.ns_per_cancel);
        bbos.push_back(r.ns_per_bbo);
        cycles_list.push_back(r.cycles);
        pf_list.push_back(r.page_faults);
    }

    std::sort(total_times.begin(), total_times.end());
    std::sort(adds.begin(), adds.end());
    std::sort(cancels.begin(), cancels.end());
    std::sort(bbos.begin(), bbos.end());
    std::sort(cycles_list.begin(), cycles_list.end());
    std::sort(pf_list.begin(), pf_list.end());

    size_t mid = num_runs / 2;
    double med_total = total_times[mid];
    double med_add = adds[mid];
    double med_cancel = cancels[mid];
    double med_bbo = bbos[mid];
    uint64_t med_cycles = cycles_list[mid];
    uint64_t med_pf = pf_list[mid];

    std::cout << "\n--------------------------------------------------------\n";
    std::cout << "MEDIAN RESULTS (Implementation " << impl << "):\n";
    std::cout << "--------------------------------------------------------\n";
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "Total execution time : " << med_total << " ms\n";
    std::cout << "Average ns/add_order : " << std::setprecision(1) << med_add << " ns\n";
    std::cout << "Average ns/cancel    : " << med_cancel << " ns\n";
    std::cout << "Average ns/get_bbo   : " << med_bbo << " ns\n";
    std::cout << "Throughput           : " << std::setprecision(2)
              << (num_ops / (med_total / 1000.0)) / 1e6 << " M ops/sec\n";
    std::cout << "CPU cycles           : " << med_cycles << "\n";
    std::cout << "Page faults          : " << med_pf << "\n";
    std::cout << "Peak memory (RSS)    : " << results.front().peak_mem_kb / 1024.0 << " MB\n";

    const auto& last = results.front();
    std::cout << "\nFinal BBO:\n";
    if (last.final_bbo.has_bid) {
        std::cout << "Bid: " << std::fixed << std::setprecision(2)
                  << last.final_bbo.bid_price << " x " << last.final_bbo.bid_qty << "\n";
    } else {
        std::cout << "Bid: None\n";
    }
    if (last.final_bbo.has_ask) {
        std::cout << "Ask: " << std::fixed << std::setprecision(2)
                  << last.final_bbo.ask_price << " x " << last.final_bbo.ask_qty << "\n";
    } else {
        std::cout << "Ask: None\n";
    }
    std::cout << "\nChecksum: " << last.checksum << "\n";
    std::cout << "========================================================\n";
}

bool run_verification(int num_operations = 100000) {
    std::cout << "Running correctness test...\n\n";

    OrderBookMap map_book;
    OrderBookVector vec_book;
    OrderBookArray arr_book;

    std::mt19937 rng(42);
    std::uniform_int_distribution<int> op_dist(0, 99);
    std::uniform_int_distribution<int> side_dist(0, 1);
    std::uniform_int_distribution<long> qty_dist(1, 100);
    std::uniform_int_distribution<int> bid_dist(5000, 9999);
    std::uniform_int_distribution<int> ask_dist(10001, 15000);

    std::vector<uint64_t> live_ids;
    uint64_t next_id = 1;

    for (int i = 0; i < num_operations; ++i) {
        int roll = op_dist(rng);

        if (roll < 70 || live_ids.empty()) { // Add
            uint64_t id = next_id++;
            Side side = (side_dist(rng) == 0) ? Side::Buy : Side::Sell;
            int tick = (side == Side::Buy) ? bid_dist(rng) : ask_dist(rng);
            double price = tick * 0.01;
            long qty = qty_dist(rng);

            map_book.add_order(id, side, price, qty);
            vec_book.add_order(id, side, price, qty);
            arr_book.add_order(id, side, price, qty);
            live_ids.push_back(id);
        } else if (roll < 90) { // Cancel
            std::uniform_int_distribution<size_t> idx_dist(0, live_ids.size() - 1);
            size_t idx = idx_dist(rng);
            uint64_t id_to_cancel = live_ids[idx];
            live_ids[idx] = live_ids.back();
            live_ids.pop_back();

            bool c_map = map_book.cancel_order(id_to_cancel);
            bool c_vec = vec_book.cancel_order(id_to_cancel);
            bool c_arr = arr_book.cancel_order(id_to_cancel);

            if (c_map != c_vec || c_vec != c_arr) {
                std::cerr << "Mismatch in cancel_order result at op " << i << "\n";
                return false;
            }
        } else { // BBO
            BBO bbo_map = map_book.get_bbo();
            BBO bbo_vec = vec_book.get_bbo();
            BBO bbo_arr = arr_book.get_bbo();

            if (!(bbo_map == bbo_vec) || !(bbo_vec == bbo_arr)) {
                std::cerr << "Mismatch in get_bbo at op " << i << "\n";
                return false;
            }
        }

        if ((i + 1) % 100 == 0) {
            auto bids_map = map_book.get_bids(10);
            auto bids_vec = vec_book.get_bids(10);
            auto bids_arr = arr_book.get_bids(10);

            if (bids_map != bids_vec || bids_vec != bids_arr) {
                std::cerr << "Mismatch in get_bids(10) at op " << i << "\n";
                return false;
            }

            auto asks_map = map_book.get_asks(10);
            auto asks_vec = vec_book.get_asks(10);
            auto asks_arr = arr_book.get_asks(10);

            if (asks_map != asks_vec || asks_vec != asks_arr) {
                std::cerr << "Mismatch in get_asks(10) at op " << i << "\n";
                return false;
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
    std::cout << "  " << prog << " --impl <A|B|C> [--runs <N>] [--ops <N>]\n";
    std::cout << "  " << prog << " --verify\n\n";
    std::cout << "Options:\n";
    std::cout << "  --impl <A|B|C>  Select implementation: A (std::map), B (std::vector), C (Direct Array)\n";
    std::cout << "  --verify        Run correctness verification across all implementations\n";
    std::cout << "  --runs <N>      Number of benchmark runs (default: 5)\n";
    std::cout << "  --ops <N>       Number of operations (default: 1,000,000)\n";
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    std::string impl = "";
    bool verify_mode = false;
    int runs = 5;
    int ops = 1000000;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--verify") {
            verify_mode = true;
        } else if (arg == "--impl") {
            if (i + 1 < argc) {
                impl = argv[++i];
            } else {
                std::cerr << "Error: --impl requires an argument (A, B, or C)\n";
                return 1;
            }
        } else if (arg == "--runs") {
            if (i + 1 < argc) {
                runs = std::stoi(argv[++i]);
            }
        } else if (arg == "--ops") {
            if (i + 1 < argc) {
                ops = std::stoi(argv[++i]);
            }
        } else if (arg == "--help" || arg == "-h") {
            print_usage(argv[0]);
            return 0;
        } else {
            std::cerr << "Unknown argument: " << arg << "\n";
            print_usage(argv[0]);
            return 1;
        }
    }

    if (verify_mode) {
        bool ok = run_verification(ops > 100000 ? 100000 : ops);
        return ok ? 0 : 1;
    }

    if (impl.empty()) {
        std::cerr << "Error: Please specify --impl <A|B|C> or --verify\n";
        print_usage(argv[0]);
        return 1;
    }

    if (impl == "A" || impl == "a" || impl == "map") {
        execute_benchmark_typed<OrderBookMap>("A", "std::map", runs, ops);
    } else if (impl == "B" || impl == "b" || impl == "vector") {
        execute_benchmark_typed<OrderBookVector>("B", "std::vector", runs, ops);
    } else if (impl == "C" || impl == "c" || impl == "array") {
        execute_benchmark_typed<OrderBookArray>("C", "Direct-Indexed Array", runs, ops);
    } else {
        std::cerr << "Unknown implementation: " << impl << " (use A, B, or C)\n";
        return 1;
    }

    return 0;
}

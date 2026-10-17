#include "orderbook_a.hpp"
#include "orderbook_b.hpp"
#include "orderbook_c.hpp"
#include "workload.hpp"

#include <algorithm>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

template <typename Book>
BenchmarkMetrics run_single_benchmark(const std::vector<WorkloadOp>& ops) {
    Book book;
    int64_t checksum = 0;

    uint64_t add_ns_total = 0, cancel_ns_total = 0, bbo_ns_total = 0, depth_ns_total = 0;
    size_t add_count = 0, cancel_count = 0, bbo_count = 0, depth_count = 0;

    auto t_start = std::chrono::steady_clock::now();

    for (size_t i = 0; i < ops.size(); ++i) {
        const auto& op = ops[i];

        if (op.type == OpType::Add) {
            auto t0 = std::chrono::steady_clock::now();
            book.add_order(op.id, op.side, op.price, op.qty);
            auto t1 = std::chrono::steady_clock::now();
            add_ns_total += std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
            ++add_count;
        } else if (op.type == OpType::Cancel) {
            auto t0 = std::chrono::steady_clock::now();
            bool ok = book.cancel_order(op.id);
            auto t1 = std::chrono::steady_clock::now();
            cancel_ns_total += std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
            ++cancel_count;
            checksum += ok ? 1 : 0;
        } else {
            auto t0 = std::chrono::steady_clock::now();
            BBO bbo = book.get_bbo();
            auto t1 = std::chrono::steady_clock::now();
            bbo_ns_total += std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
            ++bbo_count;
            checksum += bbo.bid_qty + bbo.ask_qty;
        }

        if ((i + 1) % 100 == 0) {
            auto t0 = std::chrono::steady_clock::now();
            auto bids = book.get_bids(10);
            auto asks = book.get_asks(10);
            auto t1 = std::chrono::steady_clock::now();
            depth_ns_total += std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
            ++depth_count;

            if (!bids.empty()) checksum += bids[0].qty;
            if (!asks.empty()) checksum += asks[0].qty;
        }
    }

    auto t_end = std::chrono::steady_clock::now();
    double total_ms = std::chrono::duration<double, std::milli>(t_end - t_start).count();

    return BenchmarkMetrics{
        total_ms,
        add_count ? static_cast<double>(add_ns_total) / add_count : 0.0,
        cancel_count ? static_cast<double>(cancel_ns_total) / cancel_count : 0.0,
        bbo_count ? static_cast<double>(bbo_ns_total) / bbo_count : 0.0,
        depth_count ? static_cast<double>(depth_ns_total) / depth_count : 0.0,
        checksum,
        book.get_bbo()
    };
}

static double median_of_five(std::vector<double> vals) {
    std::sort(vals.begin(), vals.end());
    return vals[vals.size() / 2];
}

template <typename Book>
void run_five_trials(const std::string& label, const std::vector<WorkloadOp>& ops) {
    constexpr int NUM_RUNS = 5;
    std::vector<double> totals, adds, cancels, bbos, depths;
    BenchmarkMetrics last_run{};

    for (int r = 0; r < NUM_RUNS; ++r) {
        last_run = run_single_benchmark<Book>(ops);
        totals.push_back(last_run.total_ms);
        adds.push_back(last_run.add_ns);
        cancels.push_back(last_run.cancel_ns);
        bbos.push_back(last_run.bbo_ns);
        depths.push_back(last_run.depth_ns);
    }

    std::cout << "==========================================================" << std::endl;
    std::cout << "Benchmark Implementation : " << label << " (5-Run Median)" << std::endl;
    std::cout << "----------------------------------------------------------" << std::endl;
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "Total Workload Time      : " << median_of_five(totals) << " ms" << std::endl;
    std::cout << "Avg add_order Latency    : " << median_of_five(adds) << " ns" << std::endl;
    std::cout << "Avg cancel_order Latency : " << median_of_five(cancels) << " ns" << std::endl;
    std::cout << "Avg get_bbo Latency      : " << median_of_five(bbos) << " ns" << std::endl;
    std::cout << "Avg get_bids/asks(10)    : " << median_of_five(depths) << " ns" << std::endl;
    std::cout << "Final BBO                : Bid " << last_run.final_bbo.bid_price
              << " x " << last_run.final_bbo.bid_qty
              << " | Ask " << last_run.final_bbo.ask_price
              << " x " << last_run.final_bbo.ask_qty << std::endl;
    std::cout << "Anti-DCE Checksum        : " << last_run.checksum << std::endl;
    std::cout << "==========================================================" << std::endl;
}

int main(int argc, char* argv[]) {
    if (argc == 2 && std::string(argv[1]) == "--verify") {
        return run_verification() ? 0 : 1;
    }

    if (argc == 3 && std::string(argv[1]) == "--impl") {
        std::string impl = argv[2];
        auto ops = generate_workload(1000000);

        if (impl == "A") {
            run_five_trials<OrderBookA>("Impl A (std::map + std::unordered_map)", ops);
            return 0;
        } else if (impl == "B") {
            run_five_trials<OrderBookB>("Impl B (Flat Sorted Vector + Open-Addressing Hash)", ops);
            return 0;
        } else if (impl == "C") {
            run_five_trials<OrderBookC>("Impl C (Direct Array + 64-bit Hardware Bitset + Direct Pool)", ops);
            return 0;
        }
    }

    std::cerr << "Usage:\n"
              << "  ./bench --verify\n"
              << "  ./bench --impl A|B|C\n";
    return 1;
}
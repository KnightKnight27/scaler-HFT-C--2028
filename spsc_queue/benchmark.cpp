#include <iostream>
#include <iomanip>
#include <thread>
#include <chrono>
#include <atomic>
#include <string>
#include <vector>
#include <cstdint>
#include <sys/resource.h>
#include "spsc_queue.hpp"

// 64-byte Order representation (matching L1 cache line size)
struct Order {
    uint64_t order_id;
    uint64_t timestamp_ns;
    double price;
    uint32_t quantity;
    char symbol[16];
    char side;          // 'B' or 'S'
    char order_type;    // 'L' or 'M'
    char padding[14];   // Pad to exactly 64 bytes
};
static_assert(sizeof(Order) == 64, "Order struct must be exactly 64 bytes");

struct BenchmarkResult {
    std::string name;
    double elapsed_sec;
    uint64_t produced;
    uint64_t consumed;
    double ops_per_sec;
    double mb_per_sec;
    double avg_latency_ns;
    long voluntary_ctx_switches;
    long involuntary_ctx_switches;
    double user_cpu_sec;
    double sys_cpu_sec;
};

template <typename QueueType>
BenchmarkResult run_1sec_benchmark(const std::string& name) {
    QueueType queue;

    std::atomic<bool> start_flag{false};
    std::atomic<bool> stop_flag{false};
    uint64_t total_produced = 0;
    uint64_t total_consumed = 0;

    struct rusage rusage_start, rusage_end;
    getrusage(RUSAGE_SELF, &rusage_start);

    std::thread producer([&]() {
        while (!start_flag.load(std::memory_order_relaxed)) {
            std::this_thread::yield();
        }

        Order order{};
        order.price = 100.0;
        order.quantity = 50;
        order.side = 'B';
        order.order_type = 'L';

        while (!stop_flag.load(std::memory_order_relaxed)) {
            order.order_id = total_produced + 1;
            if (queue.push(order)) {
                ++total_produced;
            }
        }
    });

    std::thread consumer([&]() {
        while (!start_flag.load(std::memory_order_relaxed)) {
            std::this_thread::yield();
        }

        Order order{};
        while (!stop_flag.load(std::memory_order_relaxed) || !queue.empty()) {
            if (queue.pop(order)) {
                ++total_consumed;
            }
        }
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    auto start_time = std::chrono::steady_clock::now();
    start_flag.store(true, std::memory_order_release);

    std::this_thread::sleep_for(std::chrono::seconds(1));
    stop_flag.store(true, std::memory_order_release);

    producer.join();
    consumer.join();
    auto end_time = std::chrono::steady_clock::now();
    getrusage(RUSAGE_SELF, &rusage_end);

    double elapsed_sec = std::chrono::duration<double>(end_time - start_time).count();
    double ops_per_sec = total_consumed / elapsed_sec;
    double mb_per_sec = (total_consumed * sizeof(Order)) / (elapsed_sec * 1024.0 * 1024.0);
    double avg_latency = (ops_per_sec > 0.0) ? (1e9 / ops_per_sec) : 0.0;

    long vol_switches = rusage_end.ru_nvcsw - rusage_start.ru_nvcsw;
    long invol_switches = rusage_end.ru_nivcsw - rusage_start.ru_nivcsw;

    double user_cpu = (rusage_end.ru_utime.tv_sec - rusage_start.ru_utime.tv_sec) +
                      (rusage_end.ru_utime.tv_usec - rusage_start.ru_utime.tv_usec) / 1e6;
    double sys_cpu  = (rusage_end.ru_stime.tv_sec - rusage_start.ru_stime.tv_sec) +
                      (rusage_end.ru_stime.tv_usec - rusage_start.ru_stime.tv_usec) / 1e6;

    return BenchmarkResult{
        name,
        elapsed_sec,
        total_produced,
        total_consumed,
        ops_per_sec,
        mb_per_sec,
        avg_latency,
        vol_switches,
        invol_switches,
        user_cpu,
        sys_cpu
    };
}

void print_result(const BenchmarkResult& r) {
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "\n---------------------------------------------------------\n";
    std::cout << "  Configuration: " << r.name << "\n";
    std::cout << "---------------------------------------------------------\n";
    std::cout << "  Duration              : " << r.elapsed_sec << " s\n";
    std::cout << "  Orders Pushed / Popped: " << r.produced << " / " << r.consumed << "\n";
    std::cout << "  Throughput            : " << (r.ops_per_sec / 1e6) << " Mops/sec ("
              << static_cast<uint64_t>(r.ops_per_sec) << " ops/sec)\n";
    std::cout << "  Bandwidth             : " << r.mb_per_sec << " MB/s ("
              << (r.mb_per_sec / 1024.0) << " GB/s)\n";
    std::cout << "  Avg Latency           : " << r.avg_latency_ns << " ns/op\n";
    std::cout << "  CPU Time (User / Sys) : " << r.user_cpu_sec << " s / " << r.sys_cpu_sec << " s\n";
    std::cout << "  Voluntary Ctx Switches: " << r.voluntary_ctx_switches << "\n";
    std::cout << "  Involuntary Switches  : " << r.involuntary_ctx_switches << "\n";
}

int main() {
    std::cout << "========================================================================================\n";
    std::cout << "         SPSC Queue Comprehensive Benchmark: Locks vs Lock-Free (1-sec)                  \n";
    std::cout << "         Payload: 64-byte Order struct | Ring Buffer Capacity: 65,536                   \n";
    std::cout << "========================================================================================\n";

    constexpr std::size_t kCapacity = 65536;
    std::vector<BenchmarkResult> results;

    // 1. std::mutex
    results.push_back(run_1sec_benchmark<SPSCQueue<Order, std::mutex, kCapacity>>("std::mutex"));
    print_result(results.back());

    // 2. Naive SpinLock (Test-and-Set)
    results.push_back(run_1sec_benchmark<SPSCQueue<Order, SpinLock, kCapacity>>("Naive SpinLock (TAS exchange)"));
    print_result(results.back());

    // 3. TTAS SpinLock (Spin on load)
    results.push_back(run_1sec_benchmark<SPSCQueue<Order, TTASSpinLock, kCapacity>>("TTAS SpinLock (Spin on load)"));
    print_result(results.back());

    // 4. Lock-Free Naive (False Sharing)
    results.push_back(run_1sec_benchmark<LockFreeSPSCQueueNaive<Order, kCapacity>>("Lock-Free (False Sharing Bug)"));
    print_result(results.back());

    // 5. Lock-Free SPSC (atomic head/tail with alignas(64))
    results.push_back(run_1sec_benchmark<LockFreeSPSCQueue<Order, kCapacity>>("Lock-Free (alignas(64))"));
    print_result(results.back());

    // 6. Lock-Free SPSC Cached (atomic + shadow cached indices)
    results.push_back(run_1sec_benchmark<LockFreeSPSCQueueCached<Order, kCapacity>>("Lock-Free (Cached Indices)"));
    print_result(results.back());

    std::cout << "\n========================================================================================================================\n";
    std::cout << "                                                  FINAL BENCHMARK SUMMARY                                               \n";
    std::cout << "========================================================================================================================\n";
    std::cout << std::left << std::setw(34) << "Architecture / Strategy"
              << std::setw(16) << "Throughput"
              << std::setw(14) << "Bandwidth"
              << std::setw(14) << "Avg Latency"
              << std::setw(13) << "Invol Ctx"
              << std::setw(14) << "User/Sys CPU"
              << std::setw(8)  << "Speedup" << "\n";
    std::cout << "------------------------------------------------------------------------------------------------------------------------\n";
    double baseline_ops = results.front().ops_per_sec;
    for (const auto& r : results) {
        double speedup = r.ops_per_sec / baseline_ops;
        std::string cpu_str = std::to_string(r.user_cpu_sec).substr(0, 4) + "s / " + std::to_string(r.sys_cpu_sec).substr(0, 4) + "s";
        std::cout << std::left << std::setw(34) << r.name
                  << std::setw(9) << (r.ops_per_sec / 1e6) << " Mops/s "
                  << std::setw(8) << r.mb_per_sec << " MB/s "
                  << std::setw(8) << r.avg_latency_ns << " ns/op "
                  << std::setw(13) << r.involuntary_ctx_switches
                  << std::setw(14) << cpu_str
                  << std::setw(7) << speedup << "x\n";
    }
    std::cout << "========================================================================================================================\n";

    return 0;
}

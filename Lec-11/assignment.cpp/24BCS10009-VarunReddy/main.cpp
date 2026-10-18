// How many 64-byte objects can be pushed and popped through a locked SPSC
// queue in 1 second?
//
// Thread t1 (producer) pushes sequence-numbered 64-byte objects for a fixed
// time while thread t2 (consumer) pops them; then t1.join() and t2.join().
// The consumer checks that it receives 0, 1, 2, ... in order, so every run
// also verifies that nothing was lost, duplicated or reordered.
//
//   ./SPSCQueue                                   1 second, spinlock and mutex
//   ./SPSCQueue <milliseconds> <spinlock|mutex|both> [producer_cpu consumer_cpu]

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#ifdef __linux__
#include <pthread.h>
#include <sched.h>
#endif

#include "spscqueue.h"

struct BigObject {
    uint64_t seq;
    char payload[56];
};
static_assert(sizeof(BigObject) == 64, "BigObject should be exactly one cache line");

constexpr size_t kCapacity = 1 << 16;  // 65,536 slots x 64 B = 4 MiB

struct Result {
    uint64_t pushed = 0;
    uint64_t popped = 0;
    uint64_t full_retries = 0;   // push() calls that failed because the queue was full
    uint64_t empty_retries = 0;  // pop() calls that failed because the queue was empty
    uint64_t out_of_order = 0;   // items that did not arrive in FIFO order
    double seconds = 0;
};

// Pins the calling thread to one CPU (no-op if cpu < 0 or not on Linux).
static void pin_to_cpu(int cpu) {
#ifdef __linux__
    if (cpu < 0) {
        return;
    }
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(cpu, &set);
    if (pthread_setaffinity_np(pthread_self(), sizeof(set), &set) != 0) {
        std::cerr << "warning: could not pin thread to CPU " << cpu << '\n';
    }
#else
    (void)cpu;
#endif
}

template <typename Queue>
Result run(size_t milliseconds_allowed, int producer_cpu, int consumer_cpu) {
    auto q = std::make_unique<Queue>();
    Result r;

    std::atomic<bool> go{false};             // both threads start together
    std::atomic<bool> running{true};         // main -> producer: keep pushing
    std::atomic<bool> producer_done{false};  // producer -> consumer: no more items

    // Each thread keeps its own counters in local variables and writes them to
    // `r` only once at the end, so the threads never share a cache line while running.
    std::thread t1([&] {  // producer
        pin_to_cpu(producer_cpu);
        while (!go.load(std::memory_order_acquire)) {
            cpu_relax();
        }
        uint64_t seq = 0, full = 0;
        BigObject obj{};
        while (running.load(std::memory_order_relaxed)) {
            obj.seq = seq;
            if (q->push(obj)) {
                ++seq;
            } else {
                ++full;
            }
        }
        r.pushed = seq;
        r.full_retries = full;
        producer_done.store(true, std::memory_order_release);
    });

    std::thread t2([&] {  // consumer
        pin_to_cpu(consumer_cpu);
        while (!go.load(std::memory_order_acquire)) {
            cpu_relax();
        }
        uint64_t expected = 0, empty = 0, bad = 0;
        BigObject obj;
        auto check = [&] {
            bad += (obj.seq != expected);
            ++expected;
        };
        for (;;) {
            if (q->pop(obj)) {
                check();
                continue;
            }
            if (producer_done.load(std::memory_order_acquire)) {
                // The producer has stopped and every push it made is now visible: drain.
                while (q->pop(obj)) {
                    check();
                }
                break;
            }
            ++empty;
        }
        r.popped = expected;
        r.empty_retries = empty;
        r.out_of_order = bad;
    });

    const auto start = std::chrono::steady_clock::now();
    go.store(true, std::memory_order_release);
    std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds_allowed));
    running.store(false, std::memory_order_relaxed);
    t1.join();
    t2.join();
    r.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    return r;
}

static Result run_named(const std::string& impl, size_t ms, int producer_cpu, int consumer_cpu) {
    if (impl == "spinlock") {
        return run<SpinQueue<BigObject, kCapacity>>(ms, producer_cpu, consumer_cpu);
    }
    return run<MutexQueue<BigObject, kCapacity>>(ms, producer_cpu, consumer_cpu);
}

static bool report(const std::string& impl, size_t ms, int producer_cpu, int consumer_cpu, const Result& r) {
    const bool ok = r.out_of_order == 0 && r.pushed == r.popped;
    const uint64_t total_ops = r.pushed + r.popped;

    std::cout << "Lock: " << impl << '\n';
    std::cout << "Time allowed (in ms): " << ms << '\n';
    std::cout << "Producer CPU: " << producer_cpu << ", consumer CPU: " << consumer_cpu << " (-1 = not pinned)\n";
    std::cout << "Push operations: " << r.pushed << '\n';
    std::cout << "Pop operations: " << r.popped << '\n';
    std::cout << "Total ops: " << total_ops << '\n';
    std::cout << "64-byte objects pushed+popped per second: " << static_cast<uint64_t>(r.popped / r.seconds) << '\n';
    std::cout << "Ops per second (push + pop): " << static_cast<uint64_t>(total_ops / r.seconds) << '\n';
    std::cout << "Failed pushes (queue full): " << r.full_retries << '\n';
    std::cout << "Failed pops (queue empty): " << r.empty_retries << '\n';
    std::cout << "FIFO check: " << (ok ? "PASS" : "FAIL") << " (" << r.out_of_order << " out of order)\n\n";
    return ok;
}

int main(int argc, char** argv) {
    if (argc != 1 && argc != 3 && argc != 5) {
        std::cout << "Usage: ./SPSCQueue [<milliseconds> <spinlock|mutex|both> [producer_cpu consumer_cpu]]\n";
        return -1;
    }

    const size_t ms = argc >= 3 ? std::stoul(argv[1]) : 1000;
    const std::string which = argc >= 3 ? argv[2] : "both";
    const int producer_cpu = argc == 5 ? std::stoi(argv[3]) : -1;
    const int consumer_cpu = argc == 5 ? std::stoi(argv[4]) : -1;

    std::vector<std::string> impls;
    if (which == "both") {
        impls = {"spinlock", "mutex"};
    } else if (which == "spinlock" || which == "mutex") {
        impls = {which};
    } else {
        std::cout << "Unknown lock type '" << which << "' (expected spinlock, mutex or both)\n";
        return -1;
    }

    bool ok = true;
    for (const std::string& impl : impls) {
        ok &= report(impl, ms, producer_cpu, consumer_cpu, run_named(impl, ms, producer_cpu, consumer_cpu));
    }
    return ok ? 0 : 1;
}

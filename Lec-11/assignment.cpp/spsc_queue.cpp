// WRITE AN SPSC QUEUE
// SPINLOCK ( WHILE LOOP) OR STD::MUTEX
// t1.join()  t2.join()
// producer consumer to push objects and pop objects
//
// you need to figure out a way that with locks how many
// 64 byte objects can u push and pop in 1 second
//  raise a git PR for the same
//  add readme for ur per second specs
//  feel free to add worst code qaulity :)
//
//
// ^^ MEMORY POOL ^^

// compile: g++ -O2 -std=c++17 -pthread spsc_queue.cpp -o spsc_queue
// run:     ./spsc_queue

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>

#include "spsc_queue.hpp"

// the thing we move around: exactly one 64 byte cache line
struct alignas(64) Msg {
    std::uint64_t seq;
    char payload[56];
};
static_assert(sizeof(Msg) == 64, "Msg must be 64 bytes");

constexpr std::size_t QUEUE_SIZE = 1024;
constexpr std::size_t BATCH = 32;
constexpr int RUNS = 3;

struct Result {
    std::uint64_t pushed;
    std::uint64_t popped;
    bool order_ok;
};

// batch == 1 -> normal push/pop (lock per object)
// batch  > 1 -> push_n/pop_n   (lock per batch)
template <typename Lock>
Result run_once(std::size_t batch) {
    SPSCQueue<Msg, Lock> q(QUEUE_SIZE);
    std::atomic<bool> stop{false};
    Result r{0, 0, true};

    std::thread t1([&] { // producer
        Msg buf[BATCH];
        for (auto& m : buf) std::memset(m.payload, 'x', sizeof(m.payload));
        std::uint64_t seq = 0;
        while (!stop.load(std::memory_order_relaxed)) {
            if (batch == 1) {
                buf[0].seq = seq;
                if (q.push(buf[0])) ++seq;
            } else {
                for (std::size_t i = 0; i < batch; ++i) buf[i].seq = seq + i;
                seq += q.push_n(buf, batch);
            }
        }
        r.pushed = seq;
    });

    std::thread t2([&] { // consumer
        Msg buf[BATCH];
        std::uint64_t expect = 0;
        bool ok = true;
        while (!stop.load(std::memory_order_relaxed)) {
            if (batch == 1) {
                if (q.pop(buf[0])) {
                    ok &= (buf[0].seq == expect);
                    ++expect;
                }
            } else {
                std::size_t n = q.pop_n(buf, batch);
                for (std::size_t i = 0; i < n; ++i) ok &= (buf[i].seq == expect + i);
                expect += n;
            }
        }
        r.popped = expect;
        r.order_ok = ok;
    });

    std::this_thread::sleep_for(std::chrono::seconds(1));
    stop.store(true);

    t1.join();
    t2.join();
    return r;
}

template <typename Lock>
void bench(const char* name, std::size_t batch) {
    std::uint64_t best = 0, total = 0;
    bool ok = true;
    for (int i = 0; i < RUNS; ++i) {
        Result r = run_once<Lock>(batch);
        if (r.popped > best) best = r.popped;
        total += r.popped;
        ok &= r.order_ok;
    }
    std::uint64_t avg = total / RUNS;
    std::printf("%-14s batch=%-3zu  avg %12llu obj/s  best %12llu obj/s  (%7.1f MB/s)  order %s\n",
                name, batch, (unsigned long long)avg, (unsigned long long)best,
                avg * 64.0 / (1024.0 * 1024.0), ok ? "ok" : "BROKEN");
}

int main() {
    std::printf("sizeof(Msg) = %zu bytes, queue slots = %zu, 1 second per run, %d runs each\n\n",
                sizeof(Msg), QUEUE_SIZE, RUNS);

    bench<std::mutex>("std::mutex", 1);
    bench<SpinLock>("spinlock", 1);
    bench<TTASSpinLock>("ttas spinlock", 1);
    std::printf("\n");
    bench<std::mutex>("std::mutex", BATCH);
    bench<SpinLock>("spinlock", BATCH);
    bench<TTASSpinLock>("ttas spinlock", BATCH);
    return 0;
}

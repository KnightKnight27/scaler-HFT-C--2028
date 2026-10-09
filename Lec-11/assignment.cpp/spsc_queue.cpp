// WRITE AN SPSC QUEUE
// SPINLOCK ( WHILE LOOP) OR STD::MUTEX
// t1.join()  t2.join()
// producer consumer to push objects and pop objects
//
// you need to figure out a way that with locks how many
// 64 byte objects can u push and pop in 1 second
//  raise a git PR for the same
//  add readme for ur per second specs
//
//
// ^^ MEMORY POOL ^^
// 1. MetaProgramming
// 2. Smart Pointers

// Build: clang++ -std=c++20 -O2 -pthread spsc_queue.cpp -o spsc_queue

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#if defined(__x86_64__) || defined(_M_X64)
#include <immintrin.h>
#endif

constexpr std::size_t kCapacity = 1024;  // must be a power of two
constexpr int kRuns = 5;

// The object we push/pop: exactly 64 bytes.
struct Message {
  std::uint64_t seq;
  std::uint64_t payload[7];
};
static_assert(sizeof(Message) == 64, "Message must be 64 bytes");

inline void cpu_relax() {
#if defined(__x86_64__) || defined(_M_X64)
  _mm_pause();
#elif defined(__aarch64__)
  asm volatile("yield" ::: "memory");
#endif
}

// Test-and-test-and-set spinlock: spin on a plain load so waiting threads
// don't keep stealing the cache line with failed exchanges. The wait backs off
// exponentially; without it the two threads fight over the lock line and
// throughput drops ~3-4x (see README).
class SpinLock {
 public:
  void lock() {
    while (true) {
      if (!locked_.exchange(true, std::memory_order_acquire)) return;
      for (int spins = 1; locked_.load(std::memory_order_relaxed);) {
        for (int i = 0; i < spins; ++i) cpu_relax();
        if (spins < kMaxSpins) spins <<= 1;
      }
    }
  }
  void unlock() { locked_.store(false, std::memory_order_release); }

 private:
  static constexpr int kMaxSpins = 1024;
  std::atomic<bool> locked_{false};
};

// Fixed-capacity ring buffer. Every push/pop takes the lock, so head/tail are
// plain integers. They only ever increase; tail - head is the current size and
// `& (Capacity - 1)` turns them into a slot index.
template <typename T, std::size_t Capacity, typename Lock>
class SPSCQueue {
  static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of two");

 public:
  bool push(const T& val) {
    std::lock_guard<Lock> guard(lock_);
    if (tail_ - head_ == Capacity) return false;
    buffer_[tail_++ & (Capacity - 1)] = val;
    return true;
  }

  bool pop(T& out) {
    std::lock_guard<Lock> guard(lock_);
    if (tail_ == head_) return false;
    out = buffer_[head_++ & (Capacity - 1)];
    return true;
  }

 private:
  // Lock and indices on separate cache lines: a spinning waiter only reads the
  // lock's line, so the holder can update head/tail without that line being
  // pulled away mid-critical-section (~1.5x for SpinLock, see README).
  // 128 = Apple Silicon line size; also covers x86's 64.
  alignas(128) Lock lock_;
  alignas(128) std::size_t head_{0};
  std::size_t tail_{0};
  std::unique_ptr<T[]> buffer_ = std::make_unique<T[]>(Capacity);
};

struct RunResult {
  std::uint64_t popped;
  double seconds;
  bool in_order;
  double per_second() const { return popped / seconds; }
};

// One producer thread and one consumer thread hammer the queue for ~1 second.
// Throughput = objects that made it all the way through (pushed AND popped)
// divided by the measured elapsed time.
template <typename Lock>
RunResult run_once() {
  using clock = std::chrono::steady_clock;
  SPSCQueue<Message, kCapacity, Lock> q;

  std::atomic<bool> go{false};
  std::atomic<bool> stop{false};
  // Written only by the consumer, read only after join().
  std::uint64_t popped = 0;
  bool in_order = true;

  std::thread producer([&] {
    Message m{};
    while (!go.load(std::memory_order_acquire)) cpu_relax();
    while (!stop.load(std::memory_order_relaxed)) {
      if (q.push(m)) ++m.seq;
    }
  });

  std::thread consumer([&] {
    Message m{};
    std::uint64_t expected = 0;
    while (!go.load(std::memory_order_acquire)) cpu_relax();
    while (!stop.load(std::memory_order_relaxed)) {
      if (q.pop(m)) {
        if (m.seq != expected) in_order = false;
        ++expected;
      }
    }
    popped = expected;
  });

  auto start = clock::now();
  go.store(true, std::memory_order_release);
  std::this_thread::sleep_until(start + std::chrono::seconds(1));
  stop.store(true, std::memory_order_relaxed);
  auto end = clock::now();

  producer.join();
  consumer.join();

  return {popped, std::chrono::duration<double>(end - start).count(), in_order};
}

template <typename Lock>
void bench(const char* name) {
  std::printf("\n== %s (capacity %zu, %d runs of 1s) ==\n", name, kCapacity, kRuns);
  std::vector<double> rates;
  for (int i = 0; i < kRuns; ++i) {
    RunResult r = run_once<Lock>();
    rates.push_back(r.per_second());
    std::printf("run %d: %10llu objects in %.6f s -> %10.0f objects/s (%6.1f MB/s) %s\n",
                i + 1, static_cast<unsigned long long>(r.popped), r.seconds, r.per_second(),
                r.per_second() * sizeof(Message) / 1e6, r.in_order ? "FIFO ok" : "FIFO BROKEN");
  }
  std::sort(rates.begin(), rates.end());
  std::printf("median: %.0f objects/s  min: %.0f  max: %.0f\n", rates[rates.size() / 2],
              rates.front(), rates.back());
}

int main() {
  bench<std::mutex>("std::mutex");
  bench<SpinLock>("SpinLock");
}

// Lec-11 assignment: single-producer single-consumer queue guarded by a lock.
//
// One thread pushes 64-byte Orders, another pops them, for a fixed time.
// The program reports how many objects made it through per second for:
//   1. TAS spinlock      (while loop on test_and_set)
//   2. TTAS spinlock     (while loop on a plain load, then exchange)
//   3. std::mutex
//   4. lock-free         (bonus, Lec-12 version: no lock, indices on own lines)
//   5. lock-free+cache   (bonus, each side caches the other side's index)
//
// build: g++ -std=c++20 -O3 -march=native -Wall -Wextra -pthread spsc_queue.cpp -o spsc_queue
// run:   ./spsc_queue [seconds=1] [trials=5] [producer_cpu=-1] [consumer_cpu=-1]
//        (cpu = -1 means "let the OS decide"; pinning is Linux only)

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <new>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

#if defined(__x86_64__) || defined(__i386__)
#include <immintrin.h>
#endif

#ifdef __linux__
#include <pthread.h>
#include <sched.h>
#endif

constexpr std::size_t kCacheLine = 64;
constexpr std::size_t kQueueCapacity = 4096;  // 4096 * 64 B = 256 KiB, fits in L2

// tell the core we are spinning (x86 PAUSE / arm YIELD): saves power and
// avoids the memory-order mis-speculation flush when the spin finally exits
inline void cpuRelax() {
#if defined(__x86_64__) || defined(__i386__)
  _mm_pause();
#elif defined(__aarch64__)
  asm volatile("yield");
#endif
}

// ------------------------------------------------------------------ payload
// The object we push: an order from the capstone order book, padded so that
// one Order is exactly one cache line.
enum class Side : std::uint8_t { Buy, Sell };

struct alignas(kCacheLine) Order {
  std::uint64_t id;
  std::uint64_t timestampNs;
  std::int64_t priceTicks;  // price in ticks (100.50 -> 10050), no floating point
  std::uint32_t qty;
  Side side;
  char symbol[8];
};
static_assert(sizeof(Order) == 64, "Order must be exactly 64 bytes");
static_assert(std::is_trivially_copyable_v<Order>);

// ------------------------------------------------------------------ storage
// The memory pool: one slab of raw, uninitialised slots allocated once with
// ::operator new. Objects are placement-new'd into a slot on push and destroyed
// on pop, so the hot path never calls the heap allocator.
template <typename T>
class RingStorage {
public:
  explicit RingStorage(std::size_t capacity) : mCapacity(capacity), mMask(capacity - 1) {
    if (capacity == 0 || (capacity & mMask) != 0)
      throw std::invalid_argument("capacity must be a power of 2");
    mSlots = static_cast<T*>(::operator new(sizeof(T) * capacity, std::align_val_t{alignof(T)}));
  }
  ~RingStorage() { ::operator delete(mSlots, std::align_val_t{alignof(T)}); }

  RingStorage(const RingStorage&) = delete;
  RingStorage& operator=(const RingStorage&) = delete;

  std::size_t capacity() const { return mCapacity; }

  // Indices only ever grow (a 64-bit counter will not wrap in practice), and
  // "& mask" maps them onto the ring. Capacity is a power of 2, so this is
  // the same as "% capacity" without the division.
  T* slot(std::size_t idx) const { return mSlots + (idx & mMask); }

private:
  T* mSlots;
  std::size_t mCapacity;
  std::size_t mMask;
};

// ------------------------------------------------------------------ locks
// Naive spinlock: every iteration of the while loop is an atomic
// read-modify-write, so the waiting thread keeps stealing the lock's cache
// line in exclusive state, even while the owner is still using it.
class TasSpinLock {
public:
  void lock() {
    while (mFlag.test_and_set(std::memory_order_acquire)) {
    }
  }
  void unlock() { mFlag.clear(std::memory_order_release); }

private:
  std::atomic_flag mFlag = ATOMIC_FLAG_INIT;
};

// Test-and-test-and-set: wait with a plain load (the line stays in shared
// state, no write traffic) and only try the exchange once the lock looks free.
class TtasSpinLock {
public:
  void lock() {
    for (;;) {
      if (!mLocked.exchange(true, std::memory_order_acquire)) [[likely]]
        return;
      while (mLocked.load(std::memory_order_relaxed))
        cpuRelax();
    }
  }
  void unlock() { mLocked.store(false, std::memory_order_release); }

private:
  std::atomic<bool> mLocked{false};
};

// ------------------------------------------------------------------ queues
// Ring buffer where a single lock guards both indices. Lock can be anything
// with lock()/unlock(): TasSpinLock, TtasSpinLock or std::mutex.
//
// Layout: mStorage is read-only after construction, so it sits on its own
// cache line. The lock and both indices are always touched together under the
// lock, so they share ONE line on purpose: each push/pop moves one line between
// cores instead of two or three.
template <typename T, typename Lock>
class LockedSpscQueue {
public:
  explicit LockedSpscQueue(std::size_t capacity) : mStorage(capacity) {}
  ~LockedSpscQueue() {
    for (std::size_t i = mHead; i != mTail; ++i)
      mStorage.slot(i)->~T();
  }

  LockedSpscQueue(const LockedSpscQueue&) = delete;
  LockedSpscQueue& operator=(const LockedSpscQueue&) = delete;

  bool push(const T& val) {
    std::lock_guard<Lock> guard(mLock);
    if (mTail - mHead == mStorage.capacity()) [[unlikely]]
      return false;
    ::new (mStorage.slot(mTail)) T(val);
    ++mTail;
    return true;
  }

  // pop into a caller-owned object instead of returning by value: no extra copy
  bool pop(T& out) {
    std::lock_guard<Lock> guard(mLock);
    if (mHead == mTail) [[unlikely]]
      return false;
    T* p = mStorage.slot(mHead);
    out = std::move(*p);
    p->~T();
    ++mHead;
    return true;
  }

private:
  RingStorage<T> mStorage;
  alignas(kCacheLine) Lock mLock;
  std::size_t mHead = 0;  // next slot to pop,  guarded by mLock
  std::size_t mTail = 0;  // next slot to push, guarded by mLock
};

// Bonus (Lec-12): no lock. mTail is written only by the producer and mHead
// only by the consumer, each on its own cache line so they don't false-share.
// The release store of an index publishes the slot write before it; the
// acquire load on the other side makes that slot write visible.
//
// With CacheIndices, each side also keeps a private copy of the other side's
// index and only re-reads the shared one when its copy says full/empty, so
// most push/pop calls touch no line owned by the other core.
template <typename T, bool CacheIndices>
class LockFreeSpscQueue {
public:
  explicit LockFreeSpscQueue(std::size_t capacity) : mStorage(capacity) {}
  ~LockFreeSpscQueue() {
    const std::size_t tail = mTail.load(std::memory_order_relaxed);
    for (std::size_t i = mHead.load(std::memory_order_relaxed); i != tail; ++i)
      mStorage.slot(i)->~T();
  }

  LockFreeSpscQueue(const LockFreeSpscQueue&) = delete;
  LockFreeSpscQueue& operator=(const LockFreeSpscQueue&) = delete;

  // producer thread only
  bool push(const T& val) {
    const std::size_t tail = mTail.load(std::memory_order_relaxed);  // my own index
    if constexpr (CacheIndices) {
      if (tail - mHeadCache == mStorage.capacity()) [[unlikely]] {
        mHeadCache = mHead.load(std::memory_order_acquire);
        if (tail - mHeadCache == mStorage.capacity())
          return false;
      }
    } else {
      if (tail - mHead.load(std::memory_order_acquire) == mStorage.capacity()) [[unlikely]]
        return false;
    }
    ::new (mStorage.slot(tail)) T(val);
    mTail.store(tail + 1, std::memory_order_release);
    return true;
  }

  // consumer thread only
  bool pop(T& out) {
    const std::size_t head = mHead.load(std::memory_order_relaxed);  // my own index
    if constexpr (CacheIndices) {
      if (head == mTailCache) [[unlikely]] {
        mTailCache = mTail.load(std::memory_order_acquire);
        if (head == mTailCache)
          return false;
      }
    } else {
      if (head == mTail.load(std::memory_order_acquire)) [[unlikely]]
        return false;
    }
    T* p = mStorage.slot(head);
    out = std::move(*p);
    p->~T();
    mHead.store(head + 1, std::memory_order_release);
    return true;
  }

private:
  RingStorage<T> mStorage;
  alignas(kCacheLine) std::atomic<std::size_t> mTail{0};  // written by producer
  alignas(kCacheLine) std::size_t mHeadCache = 0;         // producer-private
  alignas(kCacheLine) std::atomic<std::size_t> mHead{0};  // written by consumer
  alignas(kCacheLine) std::size_t mTailCache = 0;         // consumer-private
};

// ------------------------------------------------------------------ benchmark
struct Config {
  double seconds = 1.0;
  int trials = 5;
  int producerCpu = -1;
  int consumerCpu = -1;
};

struct Trial {
  double seconds = 0;
  std::uint64_t pushed = 0;
  std::uint64_t popped = 0;
  std::uint64_t pushFails = 0;  // push attempts that found the queue full
  std::uint64_t popFails = 0;   // pop attempts that found the queue empty
  bool fifoOk = true;
};

void pinToCpu(int cpu) {
#ifdef __linux__
  if (cpu < 0)
    return;
  cpu_set_t set;
  CPU_ZERO(&set);
  CPU_SET(cpu, &set);
  if (pthread_setaffinity_np(pthread_self(), sizeof(set), &set) != 0)
    std::fprintf(stderr, "warning: could not pin thread to cpu %d\n", cpu);
#else
  (void)cpu;
#endif
}

template <typename Queue>
Trial runTrial(const Config& cfg) {
  using Clock = std::chrono::steady_clock;

  // the queue lives on the heap: with the lock-free variants it is ~256 KiB
  auto q = std::make_unique<Queue>(kQueueCapacity);
  std::atomic<int> ready{0};
  std::atomic<bool> go{false};
  std::atomic<bool> stop{false};
  Trial result;

  std::thread producer([&] {
    pinToCpu(cfg.producerCpu);
    ready.fetch_add(1);
    while (!go.load(std::memory_order_acquire))
      cpuRelax();

    Order order{};
    order.side = Side::Buy;
    order.priceTicks = 10050;
    order.qty = 20;
    std::memcpy(order.symbol, "RELIANCE", sizeof(order.symbol));

    std::uint64_t id = 0, fails = 0;
    while (!stop.load(std::memory_order_relaxed)) {
      order.id = id;
      if (q->push(order))
        ++id;
      else
        ++fails;
    }
    result.pushed = id;
    result.pushFails = fails;
  });

  std::thread consumer([&] {
    pinToCpu(cfg.consumerCpu);
    ready.fetch_add(1);
    while (!go.load(std::memory_order_acquire))
      cpuRelax();

    Order order;
    std::uint64_t expected = 0, fails = 0;
    bool fifoOk = true;
    while (!stop.load(std::memory_order_relaxed)) {
      if (q->pop(order)) {
        fifoOk &= (order.id == expected);  // ids must come out 0, 1, 2, ...
        ++expected;
      } else {
        ++fails;
      }
    }
    result.popped = expected;
    result.popFails = fails;
    result.fifoOk = fifoOk;
  });

  while (ready.load() < 2)
    std::this_thread::yield();

  const auto start = Clock::now();
  go.store(true, std::memory_order_release);
  std::this_thread::sleep_for(std::chrono::duration<double>(cfg.seconds));
  stop.store(true, std::memory_order_relaxed);
  const auto end = Clock::now();

  producer.join();
  consumer.join();
  result.seconds = std::chrono::duration<double>(end - start).count();
  return result;
}

template <typename Queue>
void benchmark(const char* name, const Config& cfg) {
  std::vector<double> rates;  // objects popped per second, one per trial
  std::uint64_t pushed = 0, popped = 0, pushFails = 0, popFails = 0;
  bool fifoOk = true;

  for (int i = 0; i < cfg.trials; ++i) {
    const Trial t = runTrial<Queue>(cfg);
    rates.push_back(static_cast<double>(t.popped) / t.seconds);
    pushed += t.pushed;
    popped += t.popped;
    pushFails += t.pushFails;
    popFails += t.popFails;
    fifoOk &= t.fifoOk;
  }

  std::sort(rates.begin(), rates.end());
  const double median = rates[rates.size() / 2];
  const double emptyPct = 100.0 * static_cast<double>(popFails) / static_cast<double>(popFails + popped);
  const double fullPct = 100.0 * static_cast<double>(pushFails) / static_cast<double>(pushFails + pushed);

  std::printf("%-18s %9.2f %10.2f %9.2f %11.2f %10.1f %10.1f   %s\n", name, rates.front() / 1e6,
              median / 1e6, rates.back() / 1e6, median * sizeof(Order) / 1e9, emptyPct, fullPct,
              fifoOk ? "ok" : "FAIL");
}

int main(int argc, char** argv) {
  Config cfg;
  if (argc > 1) cfg.seconds = std::atof(argv[1]);
  if (argc > 2) cfg.trials = std::atoi(argv[2]);
  if (argc > 3) cfg.producerCpu = std::atoi(argv[3]);
  if (argc > 4) cfg.consumerCpu = std::atoi(argv[4]);
  if (cfg.seconds <= 0 || cfg.trials < 1) {
    std::fprintf(stderr, "usage: %s [seconds>0] [trials>=1] [producer_cpu] [consumer_cpu]\n", argv[0]);
    return 1;
  }

  std::printf("SPSC queue, %zu-byte Order, capacity %zu, %.2f s x %d trials, producer cpu %d, consumer cpu %d\n\n",
              sizeof(Order), kQueueCapacity, cfg.seconds, cfg.trials, cfg.producerCpu, cfg.consumerCpu);
  std::printf("%-18s %9s %10s %9s %11s %10s %10s   %s\n", "queue", "min M/s", "median M/s", "max M/s",
              "median GB/s", "empty %", "full %", "FIFO");

  benchmark<LockedSpscQueue<Order, TasSpinLock>>("TAS spinlock", cfg);
  benchmark<LockedSpscQueue<Order, TtasSpinLock>>("TTAS spinlock", cfg);
  benchmark<LockedSpscQueue<Order, std::mutex>>("std::mutex", cfg);
  benchmark<LockFreeSpscQueue<Order, false>>("lock-free", cfg);
  benchmark<LockFreeSpscQueue<Order, true>>("lock-free + cache", cfg);
}

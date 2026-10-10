// SPSC queue with locks (spinlock and std::mutex)
// how many 64 byte objects can we push and pop in 1 second
//
// build: g++ -O0 -std=c++20 -pthread spsc_queue.cpp -o spsc
// run:   ./spsc            (5 rounds, 1 sec each, for both locks)
//        ./spsc 10         (10 rounds)
//        ./spsc 3 spin     (only spinlock)   ./spsc 3 mutex   (only std::mutex)
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <new>
#include <thread>
#include <utility>

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__)
#include <immintrin.h>
#define CPU_RELAX() _mm_pause()
#else
#define CPU_RELAX() ((void)0)
#endif

// 64 bytes = 1 cache line
struct alignas(64) Order {
  long long id;
  char data[56];
};
static_assert(sizeof(Order) == 64, "Order should be 64 bytes");

// spinlock - while loop till the flag is free, never sleeps
class SpinLock {
public:
  void lock() {
    bool expected = false;
    while (!mFlag.compare_exchange_weak(expected, true, std::memory_order_acquire,
                                        std::memory_order_relaxed)) {
      expected = false; // cas wrote the current value (true) into expected
      // only read while someone holds it, so the cache line is not
      // stolen on every try (test and test-and-set)
      while (mFlag.load(std::memory_order_relaxed))
        CPU_RELAX();
    }
  }
  void unlock() { mFlag.store(false, std::memory_order_release); }

private:
  std::atomic<bool> mFlag{false};
};

// ring buffer, size must be power of 2
// Lock = SpinLock or std::mutex, everything else is same
template <typename T, typename Lock>
class SPSC {
public:
  explicit SPSC(size_t size) : mSize(size), mMask(size - 1) {
    if (size == 0 || (size & (size - 1)) != 0) {
      std::fprintf(stderr, "size must be power of 2\n");
      std::abort();
    }
    // memory pool: raw memory allocated only once, push/pop never allocate
    mData = static_cast<T *>(::operator new(sizeof(T) * size, std::align_val_t{alignof(T)}));
  }
  SPSC(const SPSC &) = delete;
  SPSC(SPSC &&) = delete;
  SPSC &operator=(const SPSC &) = delete;
  SPSC &operator=(SPSC &&) = delete;
  ~SPSC() {
    // destroy whatever is still inside, then give the pool back
    while (mPopIdx != mPushIdx) {
      mData[mPopIdx & mMask].~T();
      mPopIdx++;
    }
    ::operator delete(mData, std::align_val_t{alignof(T)});
  }

  bool push(const T &val) {
    mLock.lock();
    if (mPushIdx - mPopIdx == mSize) [[unlikely]] { // full
      mLock.unlock();
      return false;
    }
    new (&mData[mPushIdx & mMask]) T(val); // construct inside the pool slot
    mPushIdx++;
    mLock.unlock();
    return true;
  }

  // pop into val, no extra copy for the return value
  bool pop(T &val) {
    mLock.lock();
    if (mPushIdx == mPopIdx) [[unlikely]] { // empty
      mLock.unlock();
      return false;
    }
    T &slot = mData[mPopIdx & mMask];
    val = std::move(slot);
    slot.~T();
    mPopIdx++;
    mLock.unlock();
    return true;
  }

private:
  T *mData{nullptr};
  size_t mSize{0u};
  size_t mMask{0u};
  // push/pop idx only go up (never reset), size = push - pop
  // plain size_t because the lock protects them
  size_t mPushIdx{0u};
  size_t mPopIdx{0u};
  Lock mLock;
};

struct Result {
  long long pushed;
  long long popped;
  long long pushFull;  // push calls that found the queue full
  long long popEmpty;  // pop calls that found the queue empty
  double seconds;
  bool orderOk;
};

template <typename Lock>
Result runOnce(size_t capacity) {
  SPSC<Order, Lock> q(capacity);
  std::atomic<bool> start{false};
  std::atomic<bool> stop{false};
  long long pushed = 0, popped = 0, pushFull = 0, popEmpty = 0;
  bool orderOk = true;

  // producer: push Order with id 0, 1, 2 ...
  std::thread t1([&] {
    Order o{};
    long long count = 0, full = 0; // local counters, no false sharing with consumer
    while (!start.load(std::memory_order_acquire)) {}
    while (!stop.load(std::memory_order_relaxed)) {
      o.id = count;
      if (q.push(o))
        count++;
      else
        full++;
    }
    pushed = count;
    pushFull = full;
  });

  // consumer: pop and check the ids come in same order
  std::thread t2([&] {
    Order o{};
    long long count = 0, empty = 0;
    bool ok = true;
    while (!start.load(std::memory_order_acquire)) {}
    while (!stop.load(std::memory_order_relaxed)) {
      if (q.pop(o)) {
        if (o.id != count)
          ok = false;
        count++;
      } else {
        empty++;
      }
    }
    popped = count;
    popEmpty = empty;
    orderOk = ok;
  });

  auto begin = std::chrono::steady_clock::now();
  start.store(true, std::memory_order_release);
  std::this_thread::sleep_for(std::chrono::seconds(1));
  stop.store(true, std::memory_order_relaxed);
  auto end = std::chrono::steady_clock::now();
  t1.join();
  t2.join();

  return {pushed, popped, pushFull, popEmpty,
          std::chrono::duration<double>(end - begin).count(), orderOk};
}

void print(const char *name, int round, const Result &r) {
  double perSec = r.popped / r.seconds;
  std::printf("%-10s round %d: pushed %10lld  popped %10lld  in %.3f s  ->  %6.2f M obj/s  (%6.1f MB/s)"
              "  full %10lld  empty %10lld  order %s\n",
              name, round, r.pushed, r.popped, r.seconds, perSec / 1e6,
              perSec * sizeof(Order) / (1024.0 * 1024.0), r.pushFull, r.popEmpty,
              r.orderOk ? "ok" : "WRONG");
}

int main(int argc, char **argv) {
  int rounds = argc > 1 ? std::atoi(argv[1]) : 5;
  if (rounds <= 0)
    rounds = 5;
  // optional 2nd arg: "spin" or "mutex" to run only one lock (for timing the process)
  bool runSpin = true, runMutex = true;
  if (argc > 2) {
    runSpin = std::strcmp(argv[2], "spin") == 0;
    runMutex = std::strcmp(argv[2], "mutex") == 0;
  }
  const size_t capacity = 1024; // 1024 * 64 B = 64 KB pool

  std::printf("object size: %zu bytes, queue capacity: %zu, rounds: %d\n\n",
              sizeof(Order), capacity, rounds);

  double spinTotal = 0, mutexTotal = 0;
  // spin, mutex, spin, mutex ... so both see the same machine state
  for (int i = 1; i <= rounds; i++) {
    if (runSpin) {
      Result s = runOnce<SpinLock>(capacity);
      print("spinlock", i, s);
      spinTotal += s.popped / s.seconds;
    }
    if (runMutex) {
      Result m = runOnce<std::mutex>(capacity);
      print("std::mutex", i, m);
      mutexTotal += m.popped / m.seconds;
    }
  }

  std::printf("\n");
  if (runSpin)
    std::printf("average spinlock  : %6.2f M obj/s\n", spinTotal / rounds / 1e6);
  if (runMutex)
    std::printf("average std::mutex: %6.2f M obj/s\n", mutexTotal / rounds / 1e6);
  return 0;
}

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
//
// -----------------------------------------------------------------------------
// Lock-based Single-Producer Single-Consumer (SPSC) ring buffer.
//
// The assignment is explicitly about the LOCKED version: we guard the queue
// with a lock (spinlock OR std::mutex) and then measure how many 64-byte
// objects we can push + pop through it in one second. Two lock flavours are
// provided so we can compare throughput:
//    1. SpinLock  -> busy-wait on a std::atomic_flag
//    2. std::mutex
//
// Build:
//   g++ -O2 -std=c++17 -pthread spsc_queue.cpp -o spsc_queue && ./spsc_queue
// -----------------------------------------------------------------------------

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <thread>

// A 64-byte payload object. sizeof == 64 is checked at compile time.
struct Object {
  std::uint64_t data[8];  // 8 * 8 = 64 bytes
};
static_assert(sizeof(Object) == 64, "Object must be exactly 64 bytes");

// -----------------------------------------------------------------------------
// Spinlock: busy-wait in a while loop until we grab the flag ("SPINLOCK
// ( WHILE LOOP)" from the prompt).
// -----------------------------------------------------------------------------
class SpinLock {
 public:
  void lock() {
    // spin until we flip the flag from false -> true
    while (mFlag.test_and_set(std::memory_order_acquire)) {
      // hint the CPU we're in a spin loop so the pipeline doesn't go crazy
#if defined(__x86_64__) || defined(__i386__)
      __builtin_ia32_pause();
#endif
    }
  }
  void unlock() { mFlag.clear(std::memory_order_release); }

 private:
  std::atomic_flag mFlag = ATOMIC_FLAG_INIT;
};

// -----------------------------------------------------------------------------
// Lock-based SPSC ring buffer. Size is a power of two so the modulo becomes a
// cheap bitwise AND:  idx & (SIZE - 1).
//
// The whole queue is protected by a single Lock. In a true SPSC design you
// would use atomics + memory ordering (see Lec-10/spsc_queue.h), but THIS
// assignment wants the locked version so we can see the cost of locking.
// -----------------------------------------------------------------------------
template <typename T, typename Lock>
class SPSC {
 public:
  explicit SPSC(std::size_t size) : mSize(size), mMask(size - 1) {
    // size must be a power of two for the & trick
    mData = static_cast<T*>(::operator new(sizeof(T) * size));
  }
  SPSC(const SPSC&) = delete;
  SPSC& operator=(const SPSC&) = delete;
  ~SPSC() { ::operator delete(mData); }

  bool push(const T& val) {
    mLock.lock();
    if (full()) {  // ring buffer is full, bail out
      mLock.unlock();
      return false;
    }
    mData[mPushIdx & mMask] = val;
    mPushIdx++;
    mLock.unlock();
    return true;
  }

  bool pop(T& val) {
    mLock.lock();
    if (empty()) {
      mLock.unlock();
      return false;
    }
    val = mData[mPopIdx & mMask];
    mPopIdx++;
    mLock.unlock();
    return true;
  }

 private:
  bool full() const { return (mPushIdx - mPopIdx) == mSize; }
  bool empty() const { return mPushIdx == mPopIdx; }

  std::size_t mSize{0};
  std::size_t mMask{0};
  T* mData{nullptr};
  std::size_t mPushIdx{0};  // producer-owned
  std::size_t mPopIdx{0};   // consumer-owned
  Lock mLock;
};

// -----------------------------------------------------------------------------
// Benchmark: run the producer and consumer for one second and count how many
// objects actually made it through (i.e. were popped by the consumer).
// -----------------------------------------------------------------------------
template <typename Lock>
std::uint64_t run_benchmark(const char* name) {
  constexpr std::size_t kQueueSize = 1024;  // power of two
  SPSC<Object, Lock> queue(kQueueSize);

  std::atomic<bool> stop{false};
  std::atomic<std::uint64_t> pushed{0};
  std::atomic<std::uint64_t> popped{0};

  // Producer: keep trying to push. If the queue is full we just retry.
  std::thread producer([&]() {
    Object obj{};
    std::uint64_t local = 0;
    while (!stop.load(std::memory_order_relaxed)) {
      obj.data[0] = local;  // write something so the payload isn't dead
      if (queue.push(obj)) {
        local++;
      }
    }
    pushed.store(local, std::memory_order_relaxed);
  });

  // Consumer: keep trying to pop and count successes.
  std::thread consumer([&]() {
    Object obj{};
    std::uint64_t local = 0;
    while (!stop.load(std::memory_order_relaxed)) {
      if (queue.pop(obj)) {
        local++;
      }
    }
    // drain whatever is left so pushed ~= popped
    while (queue.pop(obj)) {
      local++;
    }
    popped.store(local, std::memory_order_relaxed);
  });

  // Let it run for exactly one second.
  std::this_thread::sleep_for(std::chrono::seconds(1));
  stop.store(true, std::memory_order_relaxed);

  producer.join();
  consumer.join();

  std::uint64_t throughput = popped.load();
  std::printf("[%-10s] pushed = %12llu | popped (per second) = %12llu | ~%.2f M ops/s\n",
              name, (unsigned long long)pushed.load(),
              (unsigned long long)throughput, throughput / 1e6);
  return throughput;
}

int main() {
  std::printf("SPSC queue throughput for 64-byte objects (1 second each)\n");
  std::printf("-----------------------------------------------------------\n");
  run_benchmark<SpinLock>("SpinLock");
  run_benchmark<std::mutex>("std::mutex");
  return 0;
}

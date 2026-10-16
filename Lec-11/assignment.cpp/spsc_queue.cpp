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
// build: g++ -O2 -std=c++20 -pthread spsc_queue.cpp -o spsc_queue
// run:   ./spsc_queue [capacity] [seconds]

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <mutex>
#include <thread>

// the 64 byte object we push / pop
struct alignas(64) Msg {
  uint64_t seq;
  char payload[56];
};
static_assert(sizeof(Msg) == 64, "Msg must be 64 bytes");

// SPINLOCK -> just a while loop on an atomic flag
class SpinLock {
  public:
    void lock() {
      while (flag.test_and_set(std::memory_order_acquire)) {
        // spin on a plain load so we don't hammer the cache line with writes
        while (flag.test(std::memory_order_relaxed)) {}
      }
    }
    void unlock() { flag.clear(std::memory_order_release); }
  private:
    std::atomic_flag flag = ATOMIC_FLAG_INIT;
};

// SPSC ring buffer. all slots are allocated ONCE up front (memory pool),
// push/pop just copy into / out of a preallocated slot -> no new/delete on the hot path
template<typename T, typename Lock>
class SPSCQueue {
  public:
    explicit SPSCQueue(size_t cap) : mCap(cap), mBuf(new T[cap]) {}

    bool push(const T& val) {
      std::lock_guard<Lock> g(mLock);
      if (mCount == mCap)
        return false;            // full
      mBuf[mTail] = val;
      mTail = (mTail + 1 == mCap) ? 0 : mTail + 1;
      ++mCount;
      return true;
    }

    bool pop(T& val) {
      std::lock_guard<Lock> g(mLock);
      if (mCount == 0)
        return false;            // empty
      val = mBuf[mHead];
      mHead = (mHead + 1 == mCap) ? 0 : mHead + 1;
      --mCount;
      return true;
    }

  private:
    size_t mCap{0u};
    std::unique_ptr<T[]> mBuf;
    size_t mHead{0u};
    size_t mTail{0u};
    size_t mCount{0u};
    Lock mLock;
};

struct Result {
  uint64_t pushed;
  uint64_t popped;
  uint64_t pushFails;
  uint64_t popFails;
  bool orderOk;
  double secs;
};

template<typename Lock>
Result bench(size_t cap, double seconds) {
  SPSCQueue<Msg, Lock> q(cap);
  std::atomic<bool> stop{false};
  std::atomic<bool> producerDone{false};
  uint64_t pushed = 0, pushFails = 0;
  uint64_t popped = 0, popFails = 0;
  bool orderOk = true;

  auto start = std::chrono::steady_clock::now();

  std::thread t1([&] {   // producer
    Msg m{};
    while (!stop.load(std::memory_order_relaxed)) {
      m.seq = pushed;
      m.payload[0] = static_cast<char>(pushed);
      if (q.push(m)) ++pushed;
      else ++pushFails;
    }
    producerDone.store(true, std::memory_order_release);
  });

  std::thread t2([&] {   // consumer
    Msg m{};
    uint64_t expected = 0;
    for (;;) {
      // read the flag BEFORE popping: if producer was done and queue is still empty, it's drained
      bool done = producerDone.load(std::memory_order_acquire);
      if (q.pop(m)) {
        if (m.seq != expected) orderOk = false;
        ++expected;
        ++popped;
      } else if (done) {
        break;
      } else {
        ++popFails;
      }
    }
  });

  std::this_thread::sleep_for(std::chrono::duration<double>(seconds));
  stop.store(true, std::memory_order_relaxed);

  t1.join();
  t2.join();

  double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
  return {pushed, popped, pushFails, popFails, orderOk && pushed == popped, secs};
}

void print(const char* name, const Result& r) {
  double opsPerSec = r.popped / r.secs;
  std::cout << name << "\n"
            << "  elapsed        : " << r.secs << " s\n"
            << "  pushed         : " << r.pushed << "\n"
            << "  popped         : " << r.popped << "\n"
            << "  push (full)    : " << r.pushFails << "\n"
            << "  pop  (empty)   : " << r.popFails << "\n"
            << "  msgs / sec     : " << static_cast<uint64_t>(opsPerSec) << "\n"
            << "  MB / sec       : " << opsPerSec * sizeof(Msg) / (1024.0 * 1024.0) << "\n"
            << "  ns / msg       : " << 1e9 / opsPerSec << "\n"
            << "  FIFO order ok  : " << (r.orderOk ? "yes" : "NO !!!") << "\n\n";
}

int main(int argc, char** argv) {
  size_t cap = argc > 1 ? std::strtoull(argv[1], nullptr, 10) : 1024;
  double seconds = argc > 2 ? std::atof(argv[2]) : 1.0;

  std::cout << "SPSC queue, 64 byte objects, capacity " << cap
            << ", " << seconds << " s per run\n\n";

  print("[SpinLock]", bench<SpinLock>(cap, seconds));
  print("[std::mutex]", bench<std::mutex>(cap, seconds));
  return 0;
}

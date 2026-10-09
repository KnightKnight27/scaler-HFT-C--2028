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
#include <atomic>
#include <cassert>
#include <chrono>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
using namespace std;

constexpr int OPS = 10000000;

struct alignas(64) Data {
  char arr[64];
};

// Spin lock
class SpinLock {
  atomic<bool> locked{false};

 public:
  void lock() {
    bool expected = false;
    while (!locked.compare_exchange_weak(expected, true, memory_order_acquire))
      expected = false;
  }
  void unlock() { locked.store(false, memory_order_release); }
};

// Queue with a spin lock
template <typename T, typename Lock>
class LockQueue {
  vector<T> buf;
  size_t cap, pushIdx = 0, popIdx = 0;
  Lock lk;

 public:
  LockQueue(size_t n) : buf(n), cap(n) { assert((n & (n - 1)) == 0); }

  bool push(const T& v) {
    lock_guard<Lock> g(lk);
    if (pushIdx - popIdx == cap)
      return false;
    buf[pushIdx & (cap - 1)] = v;
    pushIdx++;
    return true;
  }
  bool pop(T& v) {
    lock_guard<Lock> g(lk);
    if (pushIdx == popIdx)
      return false;
    v = buf[popIdx & (cap - 1)];
    popIdx++;
    return true;
  }
};

// Queue with atomics only (no lock)
template <typename T>
class AtomicQueue {
  vector<T> buf;
  size_t cap;
  alignas(64) atomic<size_t> head{0};  // consumer index
  alignas(64) atomic<size_t> tail{0};  // producer index (separate cache line)
 public:
  AtomicQueue(size_t n) : buf(n), cap(n) { assert((n & (n - 1)) == 0); }

  bool push(const T& v) {
    size_t t = tail.load(memory_order_relaxed);
    if (t - head.load(memory_order_acquire) == cap)
      return false;
    buf[t & (cap - 1)] = v;
    tail.store(t + 1, memory_order_release);
    return true;
  }
  bool pop(T& v) {
    size_t h = head.load(memory_order_relaxed);
    if (h == tail.load(memory_order_acquire))
      return false;
    v = buf[h & (cap - 1)];
    head.store(h + 1, memory_order_release);
    return true;
  }
};

// Benchmark: fixed work, measure time
template <typename Q>
void run(const char* name) {
  Q q(1024);

  auto start = chrono::steady_clock::now();
  thread t1([&] {
    Data d{};
    for (int i = 0; i < OPS; i++) {
      while (!q.push(d)) {
      }
    }
  });
  thread t2([&] {
    Data d;
    for (int i = 0; i < OPS; i++) {
      while (!q.pop(d)) {
      }
    }
  });
  t1.join();
  t2.join();
  chrono::duration<double> dur = chrono::steady_clock::now() - start;

  cout << name << ": " << dur.count() << " s, "
       << (long long)(OPS / dur.count()) << " objects/sec\n";
}

int main(int argc, char** argv) {
  string mode = argc > 1 ? argv[1] : "all";
  if (mode == "spin") {
    run<LockQueue<Data, SpinLock>>("SpinLock Queue");
  } else if (mode == "mutex") {
    run<LockQueue<Data, std::mutex>>("std::mutex Queue");
  } else if (mode == "atomic") {
    run<AtomicQueue<Data>>("Atomic Lock-Free Queue");
  } else {
    run<LockQueue<Data, std::mutex>>("std::mutex Queue");
    run<LockQueue<Data, SpinLock>>("SpinLock Queue");
    run<AtomicQueue<Data>>("Atomic Lock-Free Queue");
  }
}
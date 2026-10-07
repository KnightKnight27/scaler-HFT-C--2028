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
#include <chrono>
#include <cstdint>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

using namespace std;

struct Message {
  uint64_t id;
  char payload[56];
};
static_assert(sizeof(Message) == 64);

constexpr size_t capacity = 65536;
constexpr size_t mask = capacity - 1;

class SpinlockQueue {
  Message buffer[capacity];
  size_t head = 0;
  size_t tail = 0;
  atomic<bool> locked{false};

  void lock() noexcept {
    while (locked.exchange(true, memory_order_acquire)) {
      while (locked.load(memory_order_relaxed)) {
      }
    }
  }

  void unlock() noexcept { locked.store(false, memory_order_release); }

public:
  bool push(const Message &msg) {
    lock();
    if (tail - head >= capacity) {
      unlock();
      return false;
    }
    buffer[tail & mask] = msg;
    ++tail;
    unlock();
    return true;
  }

  bool pop(Message &msg) {
    lock();
    if (head == tail) {
      unlock();
      return false;
    }
    msg = buffer[head & mask];
    ++head;
    unlock();
    return true;
  }
};

class MutexQueue {
  Message buffer[capacity];
  size_t head = 0;
  size_t tail = 0;
  mutex mtx;

public:
  bool push(const Message &msg) {
    lock_guard<mutex> lock(mtx);
    if (tail - head >= capacity) {
      return false;
    }
    buffer[tail & mask] = msg;
    ++tail;
    return true;
  }

  bool pop(Message &msg) {
    lock_guard<mutex> lock(mtx);
    if (head == tail) {
      return false;
    }
    msg = buffer[head & mask];
    ++head;
    return true;
  }
};

template <typename Queue> void runBenchmark(const string &name) {
  auto q = make_unique<Queue>();
  atomic<bool> ready{false};
  atomic<bool> stop{false};
  uint64_t pushed = 0;
  uint64_t popped = 0;

  thread t1([&]() {
    while (!ready.load(memory_order_relaxed)) {
    }
    Message msg{0, {}};
    while (!stop.load(memory_order_relaxed)) {
      if (q->push(msg)) {
        ++pushed;
        ++msg.id;
      }
    }
  });

  thread t2([&]() {
    while (!ready.load(memory_order_relaxed)) {
    }
    Message msg{};
    while (!stop.load(memory_order_relaxed)) {
      if (q->pop(msg)) {
        ++popped;
      }
    }
  });

  auto start = chrono::high_resolution_clock::now();
  ready.store(true, memory_order_relaxed);
  this_thread::sleep_for(chrono::seconds(1));
  stop.store(true, memory_order_relaxed);
  auto end = chrono::high_resolution_clock::now();

  t1.join();
  t2.join();

  chrono::duration<double> elapsed = end - start;
  double ops = static_cast<double>(popped) / elapsed.count();

  cout << name << "\n";
  cout << "  Duration:   " << elapsed.count() << " s\n";
  cout << "  Pushed:     " << pushed << " ops\n";
  cout << "  Popped:     " << popped << " ops\n";
  cout << "  Throughput: " << static_cast<uint64_t>(ops) << " ops/s\n\n";
}

int main() {
  cout << "SPSC Queue Benchmark (64-byte objects, 1.0s window)\n\n";

  runBenchmark<SpinlockQueue>("1. SpinLock (while loop)");
  this_thread::sleep_for(chrono::milliseconds(200));

  runBenchmark<MutexQueue>("2. Mutex (std::mutex)");

  return 0;
}

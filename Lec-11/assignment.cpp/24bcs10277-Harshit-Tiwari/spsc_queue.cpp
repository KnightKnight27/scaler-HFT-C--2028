// SPSC queue guarded by a lock (spinlock or std::mutex).
// t1 pushes 64 byte objects, t2 pops them, we count how many in 1 second.
//
// build: g++ -std=c++17 -O2 -pthread spsc_queue.cpp -o spsc_queue
// run:   ./spsc_queue

#include <atomic>
#include <chrono>
#include <cstdio>
#include <mutex>
#include <thread>
#include <vector>

struct Obj {
  long seq;       // lets the consumer check the order
  char pad[56];   // 8 + 56 = 64 bytes
};
static_assert(sizeof(Obj) == 64, "Obj must be 64 bytes");

// the while loop lock
struct SpinLock {
  std::atomic_flag flag = ATOMIC_FLAG_INIT;
  void lock() { while (flag.test_and_set(std::memory_order_acquire)) {} }
  void unlock() { flag.clear(std::memory_order_release); }
};

// ring buffer, one lock for both push and pop
template <class Lock>
class Queue {
  static const size_t N = 1024;
  std::vector<Obj> buf = std::vector<Obj>(N);  // memory pool, allocated once
  Lock lock;
  size_t head = 0, tail = 0;

public:
  bool push(const Obj& o) {
    std::lock_guard<Lock> g(lock);
    if (tail - head == N) return false;  // full
    buf[tail % N] = o;
    tail++;
    return true;
  }
  bool pop(Obj& o) {
    std::lock_guard<Lock> g(lock);
    if (tail == head) return false;  // empty
    o = buf[head % N];
    head++;
    return true;
  }
};

template <class Lock>
void run(const char* name) {
  Queue<Lock> q;
  std::atomic<bool> stop{false};
  long pushed = 0, popped = 0, bad = 0;

  std::thread t1([&] {
    Obj o{};
    while (!stop) {
      o.seq = pushed;
      if (q.push(o)) pushed++;
    }
  });
  std::thread t2([&] {
    Obj o;
    while (!stop) {
      if (q.pop(o)) {
        if (o.seq != popped) bad++;
        popped++;
      }
    }
  });

  std::this_thread::sleep_for(std::chrono::seconds(1));
  stop = true;
  t1.join();
  t2.join();

  std::printf("%-10s pushed=%ld popped=%ld (%.2f M/s) out_of_order=%ld\n", name, pushed, popped,
              popped / 1e6, bad);
}

int main() {
  for (int i = 0; i < 5; i++) {
    run<SpinLock>("spinlock");
    run<std::mutex>("mutex");
  }
}

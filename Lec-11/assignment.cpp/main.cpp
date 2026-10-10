#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <thread>

#include "spsc_queue.h"

uint64_t g_sink;

int main() {
  const size_t SLOTS = 4096;
  SPSC<Msg> q(SLOTS);
  std::atomic<bool> stop{false};
  uint64_t pushes = 0;
  uint64_t pops = 0;

  auto start = std::chrono::steady_clock::now();

  std::thread t1([&] {
    Msg m{1, 100, 10, 0, {}};
    while (!stop.load(std::memory_order_relaxed)) {
      if (q.push(m)) {
        m.id++;
        pushes++;
      } else {
        std::this_thread::yield();
      }
    }
  });

  std::thread t2([&] {
    Msg m;
    while (!stop.load(std::memory_order_relaxed)) {
      if (q.pop(m)) {
        pops++;
        g_sink += m.id;
      } else {
        std::this_thread::yield();
      }
    }
  });

  std::this_thread::sleep_for(std::chrono::seconds(1));
  auto end = std::chrono::steady_clock::now();
  stop.store(true);

  t1.join();
  t2.join();

  double secs = std::chrono::duration<double>(end - start).count();
  printf("sizeof(Msg) : %zu bytes\n", sizeof(Msg));
  printf("slots       : %zu\n", SLOTS);
  printf("time        : %.3f s\n", secs);
  printf("pushes      : %llu (%.2f M/s)\n", (unsigned long long)pushes, pushes / secs / 1e6);
  printf("pops        : %llu (%.2f M/s)\n", (unsigned long long)pops, pops / secs / 1e6);
}
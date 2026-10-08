#include <cstdio>
#include <thread>
#include "spinlock.h"

const int n = 10000000;

template <typename L>
void test(const char *name) {
  L lk;
  long counter = 0;

  auto work = [&] {
    for (int i = 0; i < n; i++) {
      lk.lock();
      counter++;
      lk.unlock();
    }
  };

  std::thread a(work), b(work);
  a.join();
  b.join();

  printf("%-8s %ld  (expected %d)  %s\n", name, counter, 2 * n,
         counter == 2 * n ? "ok" : "BROKEN");
}

int main() {
  test<spinlock>("spin");
  test<naiveSpin_lock>("naive");
}
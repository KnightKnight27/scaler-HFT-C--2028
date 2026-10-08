#pragma once
#include <atomic>
#include <thread>

// own mutex
// diff from spinlock: if lock is taken we dont keep spinning,
// we call yield() so the os can run some other thread
struct mymutex {
  std::atomic<bool> f{false};

  void lock() {
	while(f.exchange(true)){
	  std::this_thread::yield();
	}
  }

  void unlock() {
	f=false;
  }
};
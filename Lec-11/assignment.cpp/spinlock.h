#pragma once
#include <atomic>

// basic spinlock
// f is false when nobody has the lock, true when someone does
struct spinlock {
  std::atomic<bool> f{false};

  void lock() {
  while(f.exchange(true)){};
}

  void unlock() {
	f=false;
  }
};

//infinite loop version to check the importance of atomics while implementing spinlocks
struct naiveSpin_lock {
  bool f = false;

  void lock() {
    // while f is true, do nothing
    // then set f to true
	while(f){

	}
	f=true;
  }

  void unlock() {
    // set f to false
	f=false;
  }
};
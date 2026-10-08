#pragma once
#include <mutex>
#include "ringbuffer.h"

// ringbuffer with a lock around it
// L can be spinlock, mymutex or std::mutex, all have lock() and unlock()
template<typename T, size_t N, typename L>
class spsc_queue {
  RingBuffer<T,N> rb;
  L l;

public:
  bool push(const T& v){
	std::lock_guard<L> g(l);
	return rb.push(v);
  }

  bool pop(T& out){
	std::lock_guard<L> g(l);
	return rb.pop(out);
  }
};
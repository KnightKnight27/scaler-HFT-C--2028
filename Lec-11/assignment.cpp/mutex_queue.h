#pragma once
#include <mutex>
#include "ringbuffer.h"

// ringbuffer + std::mutex, no templates for the lock
// same thing as spsc_queue<T,N,std::mutex> but written out by hand
template<typename T, size_t N>
class mutex_queue {
  RingBuffer<T,N> rb;
  std::mutex m;

public:
  bool push(const T& v){
	m.lock();
	bool ok=rb.push(v);
	m.unlock();
	return ok;
  }

  bool pop(T& out){
	m.lock();
	bool ok=rb.pop(out);
	m.unlock();
	return ok;
  }
};
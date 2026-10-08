#pragma once
#include <cstddef>

// head and tail only go up, never reset
// we do %N only when touching the array
// full  -> tail-head==N
// empty -> tail==head
// NOT thread safe on its own, lock goes outside (see spsc_queue.h)
template<typename T, size_t N>
class RingBuffer {
    T buf[N];
    size_t head = 0;   // how many pops done
    size_t tail = 0;   // how many pushes done

public:
    bool push(const T& value) {
		if(tail-head==N)return false;
		buf[tail%N]=value;
		tail++;
        return true;
    }

    bool pop(T& out) {
		if(tail==head)return false;
		out=buf[head%N];
		head++;
        return true;
    }
};
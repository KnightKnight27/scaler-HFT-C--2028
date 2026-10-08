#include <cassert>
#include <iostream>
#include "ringbuffer.h"

int main() {
    RingBuffer<int,4> rb;   // tiny on purpose, so wrapping happens fast
    int x;
    assert(!rb.pop(x));          // starts empty
    assert(rb.push(10));
    assert(rb.push(20));
    assert(rb.pop(x) && x == 10);
    assert(rb.pop(x) && x == 20);
    assert(!rb.pop(x));          // empty again

    // fill it fully
    for(int i=0;i<4;i++) assert(rb.push(i));
    assert(!rb.push(99));        // shld fail, its full
    for(int i=0;i<4;i++) assert(rb.pop(x) && x==i);
    assert(!rb.pop(x));

    // go around a few times to check wrapping
    for(int i=0;i<10;i++){
	assert(rb.push(i));
	assert(rb.pop(x) && x==i);
    }

    std::cout<<"all tests passed\n";
}
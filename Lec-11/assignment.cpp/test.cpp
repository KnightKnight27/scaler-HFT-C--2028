#include <cassert>

#include "spsc_queue.hpp"

int main(){
    SPSCQueue<int, 3> queue;

    int value = 0;

    assert(queue.is_empty());

    assert(queue.push(10));
    assert(queue.push(20));
    assert(queue.push(30));

    assert(!queue.push(40));

    assert(queue.pop(value));
    assert(value == 10);

    assert(queue.pop(value));
    assert(value == 20);

    assert(queue.pop(value));
    assert(value == 30);

    assert(!queue.pop(value));

    return 0;
}
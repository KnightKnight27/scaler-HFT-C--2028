#include <cassert>
#include "spsc_queue.hpp"

int main() {
    SPSCQueue<int, 2> queue;
    int value = 0;

    assert(queue.empty());
    assert(!queue.pop(value));
    assert(queue.push(10));
    assert(queue.push(20));
    assert(!queue.push(30));
    assert(queue.pop(value) && value == 10);
    assert(queue.pop(value) && value == 20);
    assert(!queue.pop(value));
}

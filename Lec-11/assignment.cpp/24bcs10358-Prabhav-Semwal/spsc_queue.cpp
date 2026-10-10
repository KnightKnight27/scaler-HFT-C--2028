#include "spsc_queue.h"

using namespace std;

bool SPSCQueue::push(const Message& message) {
    lock_guard<mutex> lock(mtx);
    if (count == 1024) return false;
    buffer[tail] = message;
    tail = (tail + 1) % 1024;
    ++count;
    return true;
}

bool SPSCQueue::pop(Message& message) {
    lock_guard<mutex> lock(mtx);
    if (count == 0) return false;
    message = buffer[head];
    head = (head + 1) % 1024;
    --count;
    return true;
}

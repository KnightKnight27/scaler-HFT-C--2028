#include <atomic>
#include <chrono>
#include <iostream>
#include <mutex>
#include <thread>

#include "spsc_queue.hpp"
using namespace std;

// one producer + one consumer for 1 second with the given lock
template <typename Lock>
void run(const char* name) {
    SpscQueue<1024, Lock> queue;
    atomic<bool> stop{false};
    uint64_t pushed = 0, popped = 0;

    thread producer([&] {
        Object obj{};
        while (!stop.load(memory_order_relaxed)) {
            obj.data[0] = pushed;
            if (queue.push(obj)) ++pushed;
        }
    });

    thread consumer([&] {
        Object obj;
        while (!stop.load(memory_order_relaxed)) {
            if (queue.pop(obj)) ++popped;
        }
    });

    this_thread::sleep_for(chrono::seconds(1));
    stop = true;
    producer.join();
    consumer.join();

    cout << name << ":\n"
         << "  pushed: " << pushed << " objects/sec\n"
         << "  popped: " << popped << " objects/sec\n";
}

int main() {
    run<mutex>("std::mutex");
    run<SpinLock>("SpinLock");
}

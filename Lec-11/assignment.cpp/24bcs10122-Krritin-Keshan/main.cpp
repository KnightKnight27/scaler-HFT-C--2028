// main.cpp — how many 64 byte objects can we push and pop in 1 second (with locks)
// build: g++ -std=c++20 -O2 -pthread main.cpp -o spsc_bench
#include <atomic>
#include <chrono>
#include <iostream>
#include <mutex>
#include <thread>

#include "spsc_queue.hpp"

template <typename Lock>
void run(const char* name) {
    static SPSCQueue<Message, 1024, Lock> q;   // static: 64 KB, keep it off the stack
    std::atomic<bool> stop{false};
    std::uint64_t pushed = 0, popped = 0;

    std::thread t1([&] {   // producer
        Message msg{};
        while (!stop.load(std::memory_order_relaxed)) {
            msg.data[0] = pushed;
            if (q.push(msg)) pushed++;
        }
    });

    std::thread t2([&] {   // consumer
        Message msg;
        while (!stop.load(std::memory_order_relaxed)) {
            if (q.pop(msg)) popped++;
        }
    });

    std::this_thread::sleep_for(std::chrono::seconds(1));
    stop = true;
    t1.join();
    t2.join();

    std::cout << name << "\n"
              << "  pushed: " << pushed << " objects/sec\n"
              << "  popped: " << popped << " objects/sec\n";
}

int main() {
    run<std::mutex>("std::mutex");
    run<SpinLock>("SpinLock");
}

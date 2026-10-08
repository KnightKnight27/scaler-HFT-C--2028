#include <atomic>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <thread>

#include "spsc_queue.h"

using namespace std;
struct alignas(64) Payload {
    uint64_t words[8]{};
};

static_assert(sizeof(Payload) == 64);

int main() {
    constexpr auto duration = chrono::seconds(1);
    SPSCQueue<Payload, 1024> queue;
    atomic<bool> stop{false};
    atomic<uint64_t> pushes{0};
    atomic<uint64_t> pops{0};

    thread producer([&] {
        Payload payload;
        while (!stop.load(memory_order_relaxed)){
            if (queue.push(payload)){
                
                ++pushes;
            }
        }
    });

    thread consumer([&] {
        Payload payload;
        while (!stop.load(memory_order_relaxed)){
            if (queue.pop(payload)){
                ++pops;
            }
        }
    });

    this_thread::sleep_for(duration);
    stop.store(true, memory_order_relaxed);
    producer.join();
    consumer.join();

    cout<<"pushes/sec: "<<pushes.load()<<endl<<"pops/sec:   "<<pops.load()<<endl;
}

#include "spsc.hpp"
#include <thread>
#include <chrono>
#include <iostream>

// exactly 64 bytes
struct Obj64 {
    char data[64];
};

std::atomic<bool> keep_running{true};
std::atomic<long long> total_pushes{0};
std::atomic<long long> total_pops{0};

// pre-allocated memory-pool style buffer
SPSCQueue<Obj64, 100000> q;

void producer() {
    Obj64 obj{};
    long long pushes = 0;

    while(keep_running) {
        if(q.push(obj)) {
            pushes++;
        }
    }
    total_pushes = pushes;
}

void consumer() {
    Obj64 obj{};
    long long pops = 0;

    while(keep_running) {
        if(q.pop(obj)) {
            pops++;
        }
    }
    total_pops = pops;
}

int main() {
    std::cout << "Starting 1-second benchmark...\n";

    std::thread t1(producer);
    std::thread t2(consumer);

    // Let it rip for exactly 1 second
    std::this_thread::sleep_for(std::chrono::seconds(1));
    keep_running = false; // kill threads

    t1.join();
    t2.join();

    std::cout << "--- RESULTS ---\n";
    std::cout << "Objects Pushed: " << total_pushes << "\n";
    std::cout << "Objects Popped: " << total_pops << "\n";

    return 0;
}

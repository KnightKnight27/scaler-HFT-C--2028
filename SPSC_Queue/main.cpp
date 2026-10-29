#include <thread>
#include <iostream>
#include <atomic>
#include <chrono>
#include "spsc_queue.cpp"

constexpr size_t QUEUE_SIZE = 1024;
constexpr int NUM_MESSAGES = 10'000'000; 

struct Payload {
    std::array<char, 64> data{};
};

void producer(SpscQueue<Payload, 1024>& q, std::atomic<bool>& running, size_t& push_count) {
    Payload p;
    while (running.load(std::memory_order_relaxed)) {
        if (q.push(p)) {
            push_count++;
        } else {
            std::this_thread::yield();
        }
    }
}

void consumer(SpscQueue<Payload, 1024>& q, std::atomic<bool>& running, size_t& pop_count) {
    Payload p;
    while (running.load(std::memory_order_relaxed)) {
        if (q.pop(p)) {
            pop_count++;
        } else {
            std::this_thread::yield();
        }
    }
}

int main() {
    SpscQueue<Payload, 1024> queue;
    std::atomic<bool> running{true};
    size_t total_pushes = 0;
    size_t total_pops = 0;

    std::thread prod_thread(producer, std::ref(queue), std::ref(running), std::ref(total_pushes));
    std::thread cons_thread(consumer, std::ref(queue), std::ref(running), std::ref(total_pops));

    std::this_thread::sleep_for(std::chrono::seconds(1));

    running.store(false, std::memory_order_relaxed);

    prod_thread.join();
    cons_thread.join();

    std::cout << "Pushes in 1 sec: " << total_pushes << "\n";
    std::cout << "Pops in 1 sec:   " << total_pops << "\n";

    return 0;
}
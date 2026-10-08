#include <thread>
#include <iostream>
#include <chrono>
#include "spsc_queue.cpp"

constexpr size_t QUEUE_SIZE = 1024;
constexpr int NUM_MESSAGES = 10'000'000; 

void producer(SpscQueue<int, QUEUE_SIZE>& q, int num_messages) {
    for (int i = 0; i < num_messages; ++i) {
        while (!q.push(i)) {
            std::this_thread::yield();
        }
    }
}
void consumer(SpscQueue<int, QUEUE_SIZE>& q, int num_messages) {
    int item;
    for (int i = 0; i < num_messages; ++i) {
        while (!q.pop(item)) {
            std::this_thread::yield();
        }
    }   
}

int main() {
    SpscQueue<int, QUEUE_SIZE> queue;

    auto start = std::chrono::high_resolution_clock::now();
    std::thread prod_thread(producer, std::ref(queue), NUM_MESSAGES);
    std::thread cons_thread(consumer, std::ref(queue), NUM_MESSAGES);

    prod_thread.join();
    cons_thread.join();

    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> diff = end - start;
    std::cout << "Time: " << diff.count() << " seconds\n";
    std::cout << "Throughput: " << (NUM_MESSAGES / diff.count()) / 1'000'000.0 << " Million Ops/sec\n";
    return 0;
}
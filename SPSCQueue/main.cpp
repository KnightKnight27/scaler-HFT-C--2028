#include <iostream>
#include "spscqueue.h"
#include <chrono>
#include <cassert>
#include <atomic>
#include <thread>

struct BigObject {
    char arr[64];
};

int main(int argc, char** argv)
{
    assert(sizeof(BigObject) == 64);

    if (argc != 3) {
        std::cout << "Usage: ./SPSCQueue <milliseconds_allowed> <num_threads>\n";
        return -1;
    }

    size_t milliseconds_allowed = std::stoi(argv[1]);
    size_t num_threads = std::stoi(argv[2]);

    constexpr size_t MAX_SIZE = 1'00'000;
    SPSCQueue<BigObject, MAX_SIZE> q;

    std::atomic<bool> running = true;

    std::vector<std::pair<int, int>> counter(num_threads, {0, 0});
    auto work = [&q, &running, &counter] (int thread_idx) {
        while (running.load()) {
            if (q.push({})) {
                counter[thread_idx].first++;
            }
            if (q.pop()) {
                counter[thread_idx].second++;
            }
        }
    };

    auto start_time = std::chrono::high_resolution_clock::now();

    for (size_t i = 0; i < num_threads; i++) {
        std::thread(work, i).detach();
    }

    while (true) {
        auto now_time = std::chrono::high_resolution_clock::now();
        auto time_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now_time - start_time);
        if (time_elapsed.count() > milliseconds_allowed) {
            running.store(false, std::memory_order_relaxed);
            break;
        }
    }

    size_t push_ops = 0, pop_ops = 0;
    for (size_t i = 0; i < num_threads; i++) {
        push_ops += counter[i].first;
        pop_ops += counter[i].second;
    }

    std::cout << "Time allowed (in ms): " << milliseconds_allowed << '\n';
    std::cout << "Number of concurrent jobs: " << num_threads << '\n';
    std::cout << "Push operations: " << push_ops << '\n';
    std::cout << "Pop operations: " << pop_ops << '\n';
    std::cout << "Total ops: " << push_ops + pop_ops << '\n';

    return 0;
}

#include "spsc_queue.h"

#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>

using namespace std;

int main() {
    SPSCQueue q;
    atomic<bool> stop{false};
    uint64_t pushed = 0, popped = 0;
    bool correct = true;
    auto start = chrono::steady_clock::now();

    thread t1([&] {
        Message m;
        while (!stop) {
            m.data[0] = pushed;
            if (q.push(m)) pushed++;
            else this_thread::yield();
        }
    });

    thread t2([&] {
        Message m;
        while (!stop) {
            if (q.pop(m)) {
                if (m.data[0] != popped) correct = false;
                popped++;
            } else {
                this_thread::yield();
            }
        }
    });

    this_thread::sleep_for(chrono::seconds(1));
    stop = true;
    t1.join();
    t2.join();
    double seconds = chrono::duration<double>(chrono::steady_clock::now() - start).count();

    cout << "Prabhav Semwal | 24bcs10358\n";
    cout << "Time: " << seconds << " seconds\n";
    cout << "Pushed: " << pushed << '\n';
    cout << "Popped: " << popped << '\n';
    cout << "Objects/sec: " << popped / seconds << '\n';
    cout << "FIFO: " << (correct ? "PASS" : "FAIL") << '\n';
    return correct ? 0 : 1;
}

// WRITE AN SPSC QUEUE
// SPINLOCK ( WHILE LOOP) OR STD::MUTEX
// t1.join()  t2.join()
// producer consumer to push objects and pop objects
//
// you need to figure out a way that with locks how many
// 64 byte objects can u push and pop in 1 second
//  raise a git PR for the same
//  add readme for ur per second specs
//  feel free to add worst code qaulity :)
//
//
// ^^ MEMORY POOL ^^

// g++ -O2 -std=c++17 -pthread spsc_queue.cpp -o spsc
// ./spsc

#include <iostream>
#include <thread>
#include <mutex>
#include <atomic>
#include <chrono>
#include <cstring>

using namespace std;

// 64 byte packet
struct Packet {
    int64_t seq;
    char payload[56];
};
static_assert(sizeof(Packet) == 64, "packet is not 64 bytes");

// spinlock -> just keep trying in a while loop
class SpinLock {
    atomic<bool> locked{false};
public:
    void lock() {
        while (locked.exchange(true, memory_order_acquire)) {
            // wait till other guy unlocks
            while (locked.load(memory_order_relaxed)) {}
        }
    }
    void unlock() {
        locked.store(false, memory_order_release);
    }
};

// queue with fixed size array (memory pool)
// allocate once in constructor, no new/delete when pushing popping
template <typename T, typename Lock>
class SPSCQueue {
    T* pool;
    size_t cap;
    size_t readIdx = 0;
    size_t writeIdx = 0;
    size_t cnt = 0;
    Lock mtx;

public:
    SPSCQueue(size_t n) : cap(n) {
        pool = new T[n];
    }
    ~SPSCQueue() {
        delete[] pool;
    }
    SPSCQueue(const SPSCQueue&) = delete;
    SPSCQueue& operator=(const SPSCQueue&) = delete;

    bool push(const T& item) {
        lock_guard<Lock> g(mtx);
        if (cnt == cap) return false; // full
        pool[writeIdx] = item;
        writeIdx = (writeIdx + 1) % cap;
        cnt++;
        return true;
    }

    bool pop(T& out) {
        lock_guard<Lock> g(mtx);
        if (cnt == 0) return false; // empty
        out = pool[readIdx];
        readIdx = (readIdx + 1) % cap;
        cnt--;
        return true;
    }
};

template <typename Lock>
void bench(const char* name) {
    SPSCQueue<Packet, Lock> q(1024);
    atomic<bool> done{false};
    int64_t pushCount = 0, popCount = 0;
    bool badOrder = false;

    // producer
    thread t1([&] {
        Packet p;
        memset(p.payload, 'x', sizeof(p.payload));
        int64_t s = 0;
        while (!done.load(memory_order_relaxed)) {
            p.seq = s;
            if (q.push(p)) s++;
        }
        pushCount = s;
    });

    // consumer
    thread t2([&] {
        Packet p;
        int64_t s = 0;
        while (!done.load(memory_order_relaxed)) {
            if (q.pop(p)) {
                if (p.seq != s) badOrder = true;
                s++;
            }
        }
        popCount = s;
    });

    this_thread::sleep_for(chrono::seconds(1));
    done = true;

    t1.join();
    t2.join();

    cout << "[" << name << "]\n";
    cout << "pushed/sec : " << pushCount << "\n";
    cout << "popped/sec : " << popCount << "\n";
    cout << "MB/sec     : " << (popCount * 64) / (1024.0 * 1024.0) << "\n";
    cout << (badOrder ? "order WRONG\n" : "order ok\n");
    cout << "\n";
}

int main() {
    bench<mutex>("std::mutex");
    bench<SpinLock>("spinlock");
    return 0;
}

// SPSC queue assignment - Lec 11
// Kartik (24bcs10259)

#include <iostream>
#include <thread>
#include <atomic>
#include <chrono>
#include <mutex>

// 64 byte object that we push and pop
struct Order {
    long id;
    long data[7];
};

// simple spin lock, just a while loop on an atomic flag
class SpinLock {
    std::atomic<bool> locked{false};
public:
    void lock() {
        while (locked.exchange(true, std::memory_order_acquire)) {
            // spin on a normal load until it looks free, so we are not
            // doing exchange (a write) in a loop and bouncing the cache line
            while (locked.load(std::memory_order_relaxed)) {}
        }
    }
    void unlock() {
        locked.store(false, std::memory_order_release);
    }
};

// ring buffer queue, one producer and one consumer
// Lock can be SpinLock or std::mutex (both have lock() / unlock())
const int SIZE = 1024;

template <typename Lock>
class LockQueue {
    Order buf[SIZE];
    int head = 0;  // consumer reads from here
    int tail = 0;  // producer writes here
    Lock lk;
public:
    bool push(const Order& o) {
        lk.lock();
        int next = (tail + 1) % SIZE;
        if (next == head) {  // full
            lk.unlock();
            return false;
        }
        buf[tail] = o;
        tail = next;
        lk.unlock();
        return true;
    }

    bool pop(Order& o) {
        lk.lock();
        if (head == tail) {  // empty
            lk.unlock();
            return false;
        }
        o = buf[head];
        head = (head + 1) % SIZE;
        lk.unlock();
        return true;
    }
};

// run producer and consumer for 1 second and count how many orders got popped
template <typename Q>
void benchmark(const char* name) {
    Q* q = new Q();  // heap, the buffer is 64KB
    std::atomic<bool> stop{false};
    long pushed = 0, popped = 0;
    bool ok = true;

    std::thread producer([&]() {
        long i = 0;
        while (!stop.load(std::memory_order_relaxed)) {
            Order o;
            o.id = i;
            if (q->push(o)) i++;
        }
        pushed = i;
    });

    std::thread consumer([&]() {
        long i = 0;
        Order o;
        while (!stop.load(std::memory_order_relaxed)) {
            if (q->pop(o)) {
                if (o.id != i) ok = false;
                i++;
            }
        }
        popped = i;
    });

    std::this_thread::sleep_for(std::chrono::seconds(1));
    stop = true;

    producer.join();
    consumer.join();

    std::cout << name << ": pushed " << pushed << ", popped " << popped
              << " in 1 second (" << popped / 1000000.0 << " M ops/sec)"
              << (ok ? "" : "  ORDER WRONG!") << "\n";
    delete q;
}

int main() {
    std::cout << "sizeof(Order) = " << sizeof(Order) << " bytes\n";
    benchmark<LockQueue<SpinLock>>("spinlock  ");
    benchmark<LockQueue<std::mutex>>("std::mutex");
    return 0;
}

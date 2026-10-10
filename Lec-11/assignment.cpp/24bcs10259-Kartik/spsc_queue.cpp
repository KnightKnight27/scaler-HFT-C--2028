// SPSC queue assignment - Lec 11
// Kartik (24bcs10259)

#include <iostream>
#include <thread>
#include <atomic>

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
            // spin
        }
    }
    void unlock() {
        locked.store(false, std::memory_order_release);
    }
};

// ring buffer queue, one producer and one consumer
const int SIZE = 1024;

class SpinQueue {
    Order buf[SIZE];
    int head = 0;  // consumer reads from here
    int tail = 0;  // producer writes here
    SpinLock lk;
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

int main() {
    SpinQueue q;
    const long N = 1000000;

    std::thread producer([&]() {
        for (long i = 0; i < N; i++) {
            Order o;
            o.id = i;
            while (!q.push(o)) {}
        }
    });

    std::thread consumer([&]() {
        for (long i = 0; i < N; i++) {
            Order o;
            while (!q.pop(o)) {}
            if (o.id != i) {
                std::cout << "wrong order! got " << o.id << " expected " << i << "\n";
            }
        }
    });

    producer.join();
    consumer.join();

    std::cout << "sizeof(Order) = " << sizeof(Order) << "\n";
    std::cout << "done, pushed and popped " << N << " orders\n";
    return 0;
}

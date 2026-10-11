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

#include <atomic>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <thread>

struct Item {
    char bytes[64];
};

class Queue {
    Item* slots;
    size_t cap;
    size_t head = 0, tail = 0;
    std::mutex m;

public:
    explicit Queue(size_t n) : slots(new Item[n]), cap(n) {}
    ~Queue() { delete[] slots; }

    bool push(const Item& it) {
        std::lock_guard<std::mutex> g(m);
        if (tail - head == cap) return false;   // full
        slots[tail % cap] = it;
        ++tail;
        return true;
    }

    bool pop(Item& it) {
        std::lock_guard<std::mutex> g(m);
        if (head == tail) return false;         // empty
        it = slots[head % cap];
        ++head;
        return true;
    }
};

std::atomic<bool> running{true};

int main() {
    Queue q(1024);
    uint64_t pushed = 0, popped = 0;

    std::thread t1([&] {
        Item it{};
        uint64_t n = 0;
        while (running) {
            it.bytes[0] = (char)n;
            if (q.push(it)) ++n;
        }
        pushed = n;
    });

    std::thread t2([&] {
        Item it;
        uint64_t n = 0;
        while (running) {
            if (q.pop(it)) ++n;
        }
        popped = n;
    });

    std::this_thread::sleep_for(std::chrono::seconds(1));
    running = false;

    t1.join();
    t2.join();

    std::cout << pushed << " pushed per second\n";
    std::cout << popped << " popped per second\n";
}

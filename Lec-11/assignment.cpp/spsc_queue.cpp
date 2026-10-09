// Name: Ayush Kumar Patra
// Roll No: 24bcs10474
//
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


#include <iostream>
#include <atomic>
#include <thread>
#include <chrono>
#include <vector>
#include <cstdint>

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#include <immintrin.h>
inline void cpu_pause() {
#if defined(_MSC_VER)
    _mm_pause();
#else
    __builtin_ia32_pause();
#endif
}
#else
inline void cpu_pause() {}
#endif

struct alignas(64) Message {
    uint64_t seq;
    uint64_t timestamp;
    char payload[48];
};
static_assert(sizeof(Message) == 64, "Message size must be exactly 64 bytes");

class LockedQueue {
private:
    std::vector<Message> buffer;
    size_t capacity;
    size_t head;
    size_t tail;
    std::atomic_flag lock = ATOMIC_FLAG_INIT;

    void acquire() {
        while (lock.test_and_set(std::memory_order_acquire)) {
            cpu_pause();
        }
    }

    void release() {
        lock.clear(std::memory_order_release);
    }

public:
    LockedQueue(size_t cap) : buffer(cap), capacity(cap), head(0), tail(0) {}

    bool push(const Message& msg) {
        acquire();
        size_t next_tail = (tail + 1) % capacity;
        if (next_tail == head) {
            release();
            return false; 
        }
        buffer[tail] = msg;
        tail = next_tail;
        release();
        return true;
    }

    bool pop(Message& msg) {
        acquire();
        if (head == tail) {
            release();
            return false;
        }
        msg = buffer[head];
        head = (head + 1) % capacity;
        release();
        return true;
    }
};

class SPSCQueue {
private:
    std::vector<Message> buffer;
    size_t capacity_mask;

    alignas(64) std::atomic<size_t> tail;
    alignas(64) std::atomic<size_t> head;

public:
    SPSCQueue(size_t capacity) {
     
        size_t cap = 1;
        while (cap < capacity) cap *= 2;
        buffer.resize(cap);
        capacity_mask = cap - 1;
        tail.store(0, std::memory_order_relaxed);
        head.store(0, std::memory_order_relaxed);
    }

    bool push(const Message& msg) {
        size_t current_tail = tail.load(std::memory_order_relaxed);
        size_t next_tail = (current_tail + 1) & capacity_mask;
        if (next_tail == head.load(std::memory_order_acquire)) {
            return false;
        }
        buffer[current_tail] = msg;
        tail.store(next_tail, std::memory_order_release);
        return true;
    }

    bool pop(Message& msg) {
        size_t current_head = head.load(std::memory_order_relaxed);
        if (current_head == tail.load(std::memory_order_acquire)) {
            return false;
        }
        msg = buffer[current_head];
        head.store((current_head + 1) & capacity_mask, std::memory_order_release);
        return true;
    }
};

template <typename Queue>
void benchmark(const char* name) {
    Queue q(65536);
    std::atomic<bool> running{true};
    std::atomic<uint64_t> push_count{0};
    std::atomic<uint64_t> pop_count{0};

    std::thread producer([&]() {
        Message msg{};
        uint64_t count = 0;
        while (running.load(std::memory_order_relaxed)) {
            msg.seq = count;
            if (q.push(msg)) {
                count++;
            }
        }
        push_count.store(count, std::memory_order_relaxed);
    });

    std::thread consumer([&]() {
        Message msg{};
        uint64_t count = 0;
        while (running.load(std::memory_order_relaxed)) {
            if (q.pop(msg)) {
                count++;
            }
        }
     
        while (q.pop(msg)) {
            count++;
        }
        pop_count.store(count, std::memory_order_relaxed);
    });

    std::this_thread::sleep_for(std::chrono::seconds(1));
    running.store(false, std::memory_order_relaxed);

    producer.join();
    consumer.join();

    std::cout << "--- " << name << " Benchmark (1 Second) ---\n";
    std::cout << "Pushed: " << push_count.load() << " ops/sec\n";
    std::cout << "Popped: " << pop_count.load() << " ops/sec\n\n";
}

int main() {
    benchmark<LockedQueue>("LockedQueue (Spinlock)");
    benchmark<SPSCQueue>("SPSCQueue (Lock-Free)");
    return 0;
}

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
#include <thread>
#include <chrono>
#include <atomic>
#include <vector>
#include <mutex>
#include <cassert>
#include <string>
using namespace std;

constexpr int TOTAL_OPS = 10000000;

struct alignas(64) Item {
    char bytes[64];
};


class SpinLock {
    atomic<bool> busy{false};
public:
    void lock() {
        bool expected = false;
        while (!busy.compare_exchange_weak(
            expected,
            true,
            memory_order_acquire
        )) {
            expected = false;
        }
    }

    void unlock() {
        busy.store(false, memory_order_release);
    }
};


template <typename T, typename LockType>
class LockedQueue {

    size_t capacity;
    size_t writeIndex = 0;
    size_t readIndex = 0;
    vector<T> buffer;
    LockType queueLock;

public:

    LockedQueue(size_t size)
        : capacity(size), buffer(size) {
        assert((size & (size - 1)) == 0);
    }

    bool push(const T& value) {

        lock_guard<LockType> guard(queueLock);
        if (writeIndex - readIndex == capacity) {
            return false;
        }

        buffer[writeIndex & (capacity - 1)] = value;
        writeIndex++;
        return true;
    }


    bool pop(T& value) {

        lock_guard<LockType> guard(queueLock);
        if (writeIndex == readIndex) {
            return false;
        }
        value = buffer[readIndex & (capacity - 1)];
        readIndex++;

        return true;
    }
};


template <typename T>
class AtomicQueue {

    size_t capacity;
    vector<T> buffer;
    alignas(64) atomic<size_t> readIndex{0};
    alignas(64) atomic<size_t> writeIndex{0};

public:

    AtomicQueue(size_t size)
        : capacity(size), buffer(size) {
        assert((size & (size - 1)) == 0);
    }


    bool push(const T& value) {

        size_t write = writeIndex.load(memory_order_relaxed);
        if (write - readIndex.load(memory_order_acquire) == capacity) {
            return false;
        }

        buffer[write & (capacity - 1)] = value;
        writeIndex.store(write + 1, memory_order_release);
        return true;
    }


    bool pop(T& value) {

        size_t read = readIndex.load(memory_order_relaxed);
        if (read == writeIndex.load(memory_order_acquire)) {
            return false;
        }
        value = buffer[read & (capacity - 1)];
        readIndex.store(read + 1, memory_order_release);
        return true;
    }
};


template <typename QueueType>
void benchmark(const char* queueName) {

    QueueType queue(1024);
    Item item{};
    auto begin = chrono::steady_clock::now();

    thread producer([&]() {
        for (int i = 0; i < TOTAL_OPS; i++) {
            while (!queue.push(item)) {
            }
        }
    });


    thread consumer([&]() {

        Item result;
        for (int i = 0; i < TOTAL_OPS; i++) {
            while (!queue.pop(result)) {
            }
        }
    });


    producer.join();
    consumer.join();


    auto finish = chrono::steady_clock::now();
    chrono::duration<double> elapsed = finish - begin;
    long long perSecond =
        static_cast<long long>(TOTAL_OPS / elapsed.count());


    cout << queueName<< ": " << elapsed.count()<< " sec, "<< perSecond << " objects/sec" << endl;
}


int main(int argc, char* argv[]) {
    string selected = "all";
    if (argc > 1) {
        selected = argv[1];
    }

    if (selected == "mutex") {
        benchmark<LockedQueue<Item, mutex>>(
            "Mutex Queue"
        );
    }

    else if (selected == "spin") {
        benchmark<LockedQueue<Item, SpinLock>>(
            "SpinLock Queue"
        );
    }

    else if (selected == "atomic") {
        benchmark<AtomicQueue<Item>>(
            "Atomic Queue"
        );
    }

    else {
        benchmark<LockedQueue<Item, mutex>>(
            "Mutex Queue"
        );
        benchmark<LockedQueue<Item, SpinLock>>(
            "SpinLock Queue"
        );
        benchmark<AtomicQueue<Item>>(
            "Atomic Queue"
        );
    }

    return 0;
}
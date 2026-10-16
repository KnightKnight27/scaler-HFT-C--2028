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
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <thread>

constexpr std::size_t CAPACITY = 1024;
constexpr std::uint64_t NUM_OBJECTS = 10'000'000;

// 64 bytes per object
struct alignas(64) Object {
    std::uint64_t sequence;
    std::byte payload[56];
};

static_assert(sizeof(Object) == 64,
              "Object must be exactly 64 bytes");

// Spinlock implementation
class SpinLock {
private:
    std::atomic_flag flag = ATOMIC_FLAG_INIT;

public:
    void lock() {
        while (flag.test_and_set(std::memory_order_acquire)) {
            // busy-wait 
        }
    }

    void unlock() {
        flag.clear(std::memory_order_release);
    }
};

//  General LockType can be spinLock or std::mutex
template <typename LockType>
class SPSCQueue {
private:
    Object buffer[CAPACITY];
    std::size_t head = 0;
    std::size_t tail = 0;
    std::size_t count = 0;
    LockType lock;

public:
    bool push(const Object& obj) {
        std::lock_guard<LockType> guard(lock);

        if (count == CAPACITY) {
            return false; // queue is full
        }

        buffer[tail] = obj;
        tail = (tail + 1) % CAPACITY;
        ++count;
        return true;
    }

    bool pop(Object& obj) {
        std::lock_guard<LockType> guard(lock);

        if (count == 0) {
            return false; 
        }

        obj = buffer[head];
        head = (head + 1) % CAPACITY;
        --count;
        return true;
    }
};

template <typename LockType>
void benchmark(const char* name) {
    SPSCQueue<LockType> queue;

    std::atomic<int> ready{0};
    std::atomic<bool> start{false};
    std::uint64_t checksum = 0;

    auto producer = [&]() {
        ready.fetch_add(1, std::memory_order_release);

        while (!start.load(std::memory_order_acquire)) {
         
        }

        for (std::uint64_t i = 0; i < NUM_OBJECTS; ++i) {
            Object obj{};
            obj.sequence = i;

            while (!queue.push(obj)) {
                // queue full->retry
            }
        }
    };

    auto consumer = [&]() {
        ready.fetch_add(1, std::memory_order_release);

        while (!start.load(std::memory_order_acquire)) {
            
        }

        for (std::uint64_t i = 0; i < NUM_OBJECTS; ++i) {
            Object obj{};

            while (!queue.pop(obj)) {
                // queue empty->retry
            }

            checksum += obj.sequence;
        }
    };

    std::thread t1(producer);
    std::thread t2(consumer);

    while (ready.load(std::memory_order_acquire) != 2) {
        // wait for both threads
    }

    const auto begin = std::chrono::steady_clock::now();

    start.store(true, std::memory_order_release);

    t1.join();
    t2.join();

    const auto end = std::chrono::steady_clock::now();

    const double seconds =
        std::chrono::duration<double>(end - begin).count();

    const double objects_per_second = NUM_OBJECTS / seconds;
    const double bytes_per_second =
        objects_per_second * sizeof(Object);

    std::cout << "\n--- " << name << " ---\n";
    std::cout << "Objects transferred: " << NUM_OBJECTS << '\n';
    std::cout << "Object size: " << sizeof(Object) << " bytes\n";
    std::cout << "Time: " << seconds << " seconds\n";
    std::cout << "Push throughput: " << objects_per_second
              << " objects/sec\n";
    std::cout << "Pop throughput: " << objects_per_second
              << " objects/sec\n";
    std::cout << "Payload rate: " << bytes_per_second / 1e6
              << " MB/sec\n";
    std::cout << "Checksum: " << checksum << '\n';
}

int main() {
    std::cout << "SPSC queue benchmark\n";
    std::cout << "Objects: " << NUM_OBJECTS << '\n';
    std::cout << "Object size: " << sizeof(Object) << " bytes\n";

    benchmark<SpinLock>("Spinlock");
    benchmark<std::mutex>("std::mutex");

    return 0;
    
}

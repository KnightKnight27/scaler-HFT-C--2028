
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>

struct Object {
    std::array<std::byte, 64> data{};
};

static_assert(sizeof(Object) == 64);

template <std::size_t Capacity>
class MemoryPool {
    std::array<Object, Capacity> objects{};
    std::vector<Object*> free_objects;
    std::mutex mtx;

public:
    MemoryPool() {
        free_objects.reserve(Capacity);
        for (auto& obj : objects) {
            free_objects.push_back(&obj);
        }
    }

    Object* acquire() {
        std::lock_guard<std::mutex> lock(mtx);
        if (free_objects.empty()) return nullptr;

        Object* obj = free_objects.back();
        free_objects.pop_back();
        return obj;
    }

    void release(Object* obj) {
        std::lock_guard<std::mutex> lock(mtx);
        free_objects.push_back(obj);
    }
};

template <std::size_t Capacity>
class PointerQueue {
    std::array<Object*, Capacity> buffer{};
    std::size_t head = 0, tail = 0, count = 0;
    std::mutex mtx;

public:
    bool push(Object* obj) {
        std::lock_guard<std::mutex> lock(mtx);
        if (count == Capacity) return false;

        buffer[tail] = obj;
        tail = (tail + 1) % Capacity;
        ++count;
        return true;
    }

    bool pop(Object*& obj) {
        std::lock_guard<std::mutex> lock(mtx);
        if (count == 0) return false;

        obj = buffer[head];
        head = (head + 1) % Capacity;
        --count;
        return true;
    }
};

int main() {
    constexpr std::size_t POOL_SIZE = 1024;
    constexpr std::size_t QUEUE_SIZE = 1024;
    constexpr std::uint64_t TOTAL = 5'000'000;

    MemoryPool<POOL_SIZE> pool;
    PointerQueue<QUEUE_SIZE> queue;

    std::atomic<bool> start_flag{false};
    std::uint64_t consumed = 0;

    std::thread t1([&]() {
        while (!start_flag.load(std::memory_order_acquire)) {}

        std::uint64_t produced = 0;
        while (produced < TOTAL) {
            Object* obj = pool.acquire();
            if (obj == nullptr) continue;

            obj->data[0] = std::byte{42};

            while (!queue.push(obj)) {}

            ++produced;
        }
    });

    std::thread t2([&]() {
        while (!start_flag.load(std::memory_order_acquire)) {}

        while (consumed < TOTAL) {
            Object* obj = nullptr;

            if (queue.pop(obj)) {
                // Process the object before returning it.
                (void)obj->data[0];
                pool.release(obj);
                ++consumed;
            }
        }
    });

    const auto start = std::chrono::steady_clock::now();
    start_flag.store(true, std::memory_order_release);

    t1.join();
    t2.join();

    const auto end = std::chrono::steady_clock::now();
    const double seconds =
        std::chrono::duration<double>(end - start).count();

    std::cout << "Object size: " << sizeof(Object) << " bytes\n";
    std::cout << "Objects consumed: " << consumed << '\n';
    std::cout << "Elapsed seconds: " << seconds << '\n';
    std::cout << "Completed objects/second: "
              << consumed / seconds << '\n';
}

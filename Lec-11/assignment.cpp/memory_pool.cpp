
#include <array>
#include <cstddef>
#include <iostream>
#include <mutex>
#include <vector>

struct Object {
    std::array<std::byte, 64> data{};
};

static_assert(sizeof(Object) == 64);

template <std::size_t Capacity>
class MemoryPool {
private:
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

        if (free_objects.empty()) {
            return nullptr;
        }

        Object* obj = free_objects.back();
        free_objects.pop_back();
        return obj;
    }

    void release(Object* obj) {
        if (obj == nullptr) {
            return;
        }

        std::lock_guard<std::mutex> lock(mtx);
        free_objects.push_back(obj);
    }
};

int main() {
    MemoryPool<1024> pool;

    Object* obj = pool.acquire();

    if (obj != nullptr) {
        std::cout << "Acquired object: "
                  << sizeof(*obj) << " bytes\n";

        pool.release(obj);
        std::cout << "Object returned to pool\n";
    }
}

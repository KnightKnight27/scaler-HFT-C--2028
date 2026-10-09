#include <array>
#include <mutex>
#include <cstddef>
#include "Spin_Lock.hpp"

template<typename T, size_t Size>
class SpscQueue {
private:
    alignas(64) size_t PushIndex{0};
    alignas(64) size_t PopIndex{0};
    std::array<T, Size> Storage;
    Spinlock Lock;

public:
    bool push(const T& item) {
        std::lock_guard<Spinlock> guard(Lock);
        if (PushIndex - PopIndex < Size) {
            Storage[PushIndex % Size] = item;
            PushIndex++;
            return true;
        }
        return false;
    }

    bool pop(T& item) {
        std::lock_guard<Spinlock> guard(Lock);
        if (PushIndex > PopIndex) {
            item = Storage[PopIndex % Size];
            PopIndex++;
            return true;
        }
        return false;
    }
};
#pragma once

#include <array>
#include <cstddef>
#include <mutex>
#include <stdexcept>
#include "Spin_Lock.hpp"

template <typename T, std::size_t Capacity>
class MemoryPool {
public:
    MemoryPool() {
        for (std::size_t index = 0; index < Capacity; ++index) {
            free_indices[index] = Capacity - index - 1;
        }
    }

    T* acquire() {
        std::lock_guard<Spinlock> guard(lock);
        if (available_count == 0) {
            return nullptr;
        }

        const std::size_t index = free_indices[--available_count];
        return &storage[index];
    }

    void release(T* item) {
        if (item == nullptr) {
            return;
        }

        const std::size_t index = static_cast<std::size_t>(item - storage.data());
        if (index >= Capacity) {
            throw std::invalid_argument("MemoryPool::release received an unknown object");
        }

        std::lock_guard<Spinlock> guard(lock);
        free_indices[available_count++] = index;
    }

private:
    std::array<T, Capacity> storage{};
    std::array<std::size_t, Capacity> free_indices{};
    std::size_t available_count{Capacity};
    Spinlock lock;
};

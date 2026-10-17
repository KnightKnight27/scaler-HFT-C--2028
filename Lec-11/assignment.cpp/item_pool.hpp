#pragma once

#include "item.hpp"

#include <cstddef>
#include <memory>

template <std::size_t Capacity>
class ItemPool {
public:
    ItemPool() : items_(std::make_unique<Item[]>(Capacity)) {}

    Item& operator[](std::size_t index) noexcept {
        return items_[index];
    }

private:
    // C++17 array allocation honors Item's 64-byte over-alignment.
    std::unique_ptr<Item[]> items_;
};

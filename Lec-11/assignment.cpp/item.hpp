#pragma once

#include <cstddef>

struct alignas(64) Item {
    std::byte data[64];
};

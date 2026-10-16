#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

struct Object64 {
    std::array<std::uint64_t, 8> words{};
};

static_assert(sizeof(Object64) == 64, "The benchmark object must be 64 bytes");

// All eight words depend on the sequence number, so checking the object also
// catches stale or partially copied payloads, not just an incorrect first word.
inline Object64 make_object(std::uint64_t sequence) noexcept {
    Object64 value;
    value.words[0] = sequence;
    for (std::size_t i = 1; i < value.words.size(); ++i) {
        value.words[i] = sequence ^ (UINT64_C(0x9e3779b97f4a7c15) * i);
    }
    return value;
}

inline bool valid_object(const Object64& value, std::uint64_t sequence) noexcept {
    return value.words == make_object(sequence).words;
}

#ifndef WORKLOAD_HPP
#define WORKLOAD_HPP
#include "implementation/types.hpp"
#include <cstddef>
#include <cstdint>
#include <vector>

enum class Kind { Add, Cancel, Query };
struct Operation {
    Kind kind;
    std::uint64_t id = 0;
    Side side = Side::Buy;
    int price_ticks = 0;
    long qty = 0;
};
struct Workload {
    std::vector<Operation> operations;
    std::size_t adds = 0, cancels = 0, queries = 0;
    std::uint64_t fingerprint = 14695981039346656037ULL;
};
Workload generate_workload(std::size_t count, std::uint32_t seed);
#endif

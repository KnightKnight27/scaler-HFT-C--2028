#pragma once
// deterministic op stream shared by bench and verify
#include "order_book_api.hpp"
#include <cstdint>
#include <vector>

enum class OpType : uint8_t { Add, Cancel, Bbo };

struct Op {
    OpType type;
    Side side;
    uint64_t id;
    int32_t tick;
    int32_t qty;
};

struct Workload {
    std::vector<Op> ops;
    size_t n_add = 0, n_cancel = 0, n_bbo = 0;
    size_t degraded = 0;  // cancels that turned into adds because nothing was live
};

Workload gen_workload(size_t n_ops, uint32_t seed);

#pragma once

#include "types.hpp"
#include <cstdint>
#include <vector>

enum class OpType : uint8_t {
    Add = 0,
    Cancel = 1,
    GetBBO = 2
};

struct WorkloadOp {
    OpType type;
    Side side;
    uint64_t id;
    double price;
    long qty;
};

struct BenchmarkMetrics {
    double total_ms;
    double add_ns;
    double cancel_ns;
    double bbo_ns;
    double depth_ns;
    int64_t checksum;
    BBO final_bbo;
};

// Generates deterministic 1,000,000 operations using std::mt19937(42):
// 70% Add, 20% valid live Cancel (via swap-and-pop), 10% GetBBO.
std::vector<WorkloadOp> generate_workload(size_t num_ops = 1000000);

// Runs unit edge-case tests and full 1,000,000-op cross-verification across A, B, and C.
bool run_verification();
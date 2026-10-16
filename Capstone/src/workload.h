// workload.h — deterministic operation stream shared by the benchmark and verifier.
//
// The whole stream is generated BEFORE any timing starts, so the RNG and the
// live-order bookkeeping are never inside the timed region. Every implementation
// replays the identical vector.
//
// Depth queries (get_bids(10) + get_asks(10) after every 100th op) are NOT
// stored as Ops. They are extra to the 1M ops; the harness issues them and
// times them as their own category.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "types.h"

enum class OpType : uint8_t { Add, Cancel, Bbo };

struct Op {
    OpType type;
    Side side;     // Add only
    uint64_t id;   // Add, Cancel
    double price;  // Add only; precomputed so no conversion happens while timing
    long qty;      // Add only
};

constexpr std::size_t kDefaultNumOps = 1'000'000;
constexpr uint32_t kDefaultSeed = 42;
constexpr std::size_t kDepthQueryInterval = 100;
constexpr int kDepthQueryLevels = 10;

// Percent of ops that are Add / Cancel; the remainder are BBO queries.
struct OpMix {
    int add_percent;
    int cancel_percent;
};

// The graded workload from the assignment.
constexpr OpMix kGradedMix{70, 20};

// Verification-only stress mix (never benchmarked). With adds ≈ cancels the book stays
// thin, so levels and the best price empty constantly. The graded mix saturates every
// level within the first ~16% of ops, which leaves empty-level code paths almost unexercised.
constexpr OpMix kThinBookMix{45, 45};

struct WorkloadSpec {
    std::size_t num_ops = kDefaultNumOps;
    uint32_t seed = kDefaultSeed;
    OpMix mix = kGradedMix;
    // false: ids are 1, 2, 3, ... in order of creation (graded workload).
    // true:  ids are a bijective scramble of 1, 2, 3, ..., so still unique but spread over
    //        the whole 64-bit range (verification only). Sequential ids are kind to hash
    //        tables (few or no collisions), which hides probe-chain bugs.
    bool scrambled_ids = false;
};

// Side 50/50, qty uniform 1..100, price uniform over the side's tick band. A Cancel
// always targets a live order; if none exists, an Add is generated instead; if
// `fallback_adds` is non-null it receives how many times that happened.
// Ids are unique and never reused.
std::vector<Op> generate_workload(const WorkloadSpec& spec = WorkloadSpec{}, std::size_t* fallback_adds = nullptr);

struct WorkloadStats {
    std::size_t adds = 0;
    std::size_t cancels = 0;
    std::size_t bbos = 0;
};

WorkloadStats count_ops(const std::vector<Op>& ops);

// FNV-1a over every op field. Printed by bench and verify so it is visible that
// every implementation (and every machine) ran the identical stream.
uint64_t hash_workload(const std::vector<Op>& ops);

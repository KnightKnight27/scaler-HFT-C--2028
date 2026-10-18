// workload.h - the random operation stream used by both the benchmark and --verify.
//
// The whole stream is generated up front, so random number generation is never inside a
// timed region and every implementation replays exactly the same operations.
#pragma once

#include <cstdint>
#include <random>
#include <vector>

#include "common.h"

enum class OpType : uint8_t { Add, Cancel, Bbo };

// How order ids are handed out. The rules only say ids are unique and never reused.
//   Sequential: 1, 2, 3, ...            (the benchmark; what exchanges usually do)
//   Scrambled:  a bijective mix of 1, 2, 3, ... so ids look random
//   Strided:    1 << 20, 2 << 20, ...   (many ids share their low bits)
enum class IdPattern : uint8_t { Sequential, Scrambled, Strided };

// splitmix64 finalizer. Every step (xor-shift, multiply by an odd number) can be undone,
// so different inputs always give different outputs: scrambled ids stay unique.
inline uint64_t scramble(uint64_t x) {
    x ^= x >> 30;
    x *= 0xBF58476D1CE4E5B9ull;
    x ^= x >> 27;
    x *= 0x94D049BB133111EBull;
    x ^= x >> 31;
    return x;
}

inline OrderId make_id(IdPattern pattern, uint64_t seq) {
    switch (pattern) {
        case IdPattern::Scrambled: return scramble(seq);
        case IdPattern::Strided: return seq << 20;
        default: return seq;
    }
}

struct Op {
    OrderId id;  // Add: new id, Cancel: id of a live order, Bbo: unused
    Price price;
    int32_t qty;
    OpType type;
    Side side;
};

struct WorkloadConfig {
    uint32_t seed = 42;
    size_t num_ops = 1'000'000;
    int add_pct = 70;     // percent of ops that are add_order
    int cancel_pct = 20;  // percent that are cancel_order; the rest are get_bbo
    int band = kLevelsPerSide;  // prices come from the `band` ticks closest to the mid
    IdPattern ids = IdPattern::Sequential;
};

// The benchmark also calls get_bids(10) and get_asks(10) after every kDepthEvery ops.
constexpr size_t kDepthEvery = 100;

struct Workload {
    std::vector<Op> ops;
    size_t adds = 0, cancels = 0, bbos = 0;
};

// Uniform integer in [lo, hi]. Done by hand (multiply-shift) instead of
// std::uniform_int_distribution so the stream is the same with every standard library.
inline int64_t uniform(std::mt19937& rng, int64_t lo, int64_t hi) {
    const uint64_t range = static_cast<uint64_t>(hi - lo) + 1;
    return lo + static_cast<int64_t>((static_cast<uint64_t>(rng()) * range) >> 32);
}

inline Workload make_workload(const WorkloadConfig& cfg) {
    std::mt19937 rng(cfg.seed);
    Workload w;
    w.ops.reserve(cfg.num_ops);

    std::vector<OrderId> live;  // ids that are currently resting, so cancels always hit
    live.reserve(cfg.num_ops);
    uint64_t next_seq = 1;

    for (size_t i = 0; i < cfg.num_ops; ++i) {
        const int r = static_cast<int>(uniform(rng, 0, 99));
        Op op{};
        op.type = r < cfg.add_pct ? OpType::Add : r < cfg.add_pct + cfg.cancel_pct ? OpType::Cancel : OpType::Bbo;
        if (op.type == OpType::Cancel && live.empty()) op.type = OpType::Add;  // nothing to cancel yet

        if (op.type == OpType::Add) {
            op.id = make_id(cfg.ids, next_seq++);
            op.side = uniform(rng, 0, 1) == 0 ? Side::Buy : Side::Sell;
            op.price = op.side == Side::Buy ? static_cast<Price>(uniform(rng, kBidMax - cfg.band + 1, kBidMax))
                                            : static_cast<Price>(uniform(rng, kAskMin, kAskMin + cfg.band - 1));
            op.qty = static_cast<int32_t>(uniform(rng, 1, 100));
            live.push_back(op.id);
            ++w.adds;
        } else if (op.type == OpType::Cancel) {
            // cancel a random live order (swap-remove it from the live list)
            const size_t k = static_cast<size_t>(uniform(rng, 0, static_cast<int64_t>(live.size()) - 1));
            op.id = live[k];
            live[k] = live.back();
            live.pop_back();
            ++w.cancels;
        } else {
            ++w.bbos;
        }
        w.ops.push_back(op);
    }
    return w;
}

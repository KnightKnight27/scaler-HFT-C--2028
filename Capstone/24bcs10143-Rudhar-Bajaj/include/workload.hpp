#pragma once
// Pre-generated, repeatable workload. Generating every op up front (including which
// live order each cancel hits) means the timed loop only calls the book, and all
// three implementations see exactly the same sequence.

#include <random>

#include "common.hpp"

enum class OpType : uint8_t { Add, Cancel, Bbo, Depth };

struct Op {
    OpType type;
    Side side;
    int32_t qty;
    uint64_t id;
    double price;
};

struct WorkloadConfig {
    size_t ops = 1'000'000;  // regular ops (Depth queries come on top of these)
    uint32_t seed = 42;
    int add_pct = 70;
    int cancel_pct = 20;     // the rest (10%) are BBO queries
    int depth_every = 100;   // get_bids(10) + get_asks(10) every N ops
    int band_ticks = 0;      // 0 = full band; otherwise only the N ticks nearest the mid on each side
    bool scramble_ids = false;  // ids not in increasing order (exercises hashing / trees differently)
};

// Uniform integer in [lo, hi] from one mt19937 draw (multiply-shift, bias < 2^-20 here).
// std::uniform_int_distribution is not used on purpose: libstdc++ and libc++ implement it
// differently, so the same seed would give a different workload on Linux and macOS.
inline uint64_t draw(std::mt19937& rng, uint64_t lo, uint64_t hi) {
    return lo + ((static_cast<uint64_t>(rng()) * (hi - lo + 1)) >> 32);
}

inline std::vector<Op> make_workload(const WorkloadConfig& cfg) {
    std::mt19937 rng(cfg.seed);
    const int bid_lo = cfg.band_ticks ? ticks::kBidMax - cfg.band_ticks + 1 : ticks::kBidMin;
    const int ask_hi = cfg.band_ticks ? ticks::kAskMin + cfg.band_ticks - 1 : ticks::kAskMax;

    std::vector<Op> ops;
    ops.reserve(cfg.ops + cfg.ops / cfg.depth_every + 1);
    std::vector<uint64_t> live;  // ids currently resting; swap-and-pop keeps cancels O(1)
    live.reserve(cfg.ops);
    uint64_t next_id = 1;

    for (size_t i = 0; i < cfg.ops; ++i) {
        const int r = (int)draw(rng, 0, 99);
        if (r < cfg.add_pct || (r < cfg.add_pct + cfg.cancel_pct && live.empty())) {
            const Side side = draw(rng, 0, 1) ? Side::Sell : Side::Buy;
            const int tick = side == Side::Buy ? (int)draw(rng, bid_lo, ticks::kBidMax) : (int)draw(rng, ticks::kAskMin, ask_hi);
            uint64_t id = next_id++;
            if (cfg.scramble_ids) id = id * 0x9E3779B97F4A7C15ull;  // odd multiplier: still unique
            ops.push_back({OpType::Add, side, (int32_t)draw(rng, 1, 100), id, ticks::to_price(tick)});
            live.push_back(id);
        } else if (r < cfg.add_pct + cfg.cancel_pct) {
            const size_t k = draw(rng, 0, live.size() - 1);
            ops.push_back({OpType::Cancel, Side::Buy, 0, live[k], 0.0});
            live[k] = live.back();
            live.pop_back();
        } else {
            ops.push_back({OpType::Bbo, Side::Buy, 0, 0, 0.0});
        }
        if ((i + 1) % cfg.depth_every == 0) ops.push_back({OpType::Depth, Side::Buy, 0, 0, 0.0});
    }
    return ops;
}

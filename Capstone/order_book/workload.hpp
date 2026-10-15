// workload.hpp - deterministic random workload shared by the benchmark and the verifier.
//
// The whole operation list is generated ONCE up front (fixed seed), so
//   * generation cost is never inside a timed region, and
//   * every implementation replays byte-for-byte the same operations.
// Only std::mt19937 raw output (fully specified by the C++ standard) is used - no
// std::uniform_*_distribution, whose output differs between libstdc++ and libc++ - so the
// workload and the final checksum are identical on macOS and Linux.
#pragma once
#include <cstddef>
#include <cstdint>
#include <random>
#include <vector>

#include "common.hpp"

enum class OpType : std::uint8_t { Add, Cancel, Bbo };

struct Op {
    OrderId id;     // Add: new id.  Cancel: id of a live order.  Bbo: unused.
    long    qty;    // Add only
    Price   price;  // Add only (ticks)
    OpType  type;
    Side    side;   // Add only
};

enum class PriceModel {
    Uniform,    // spec: uniformly random valid tick across the whole band
    Clustered,  // extra: prices cluster near the touch (geometric distance), like a real book
};

// ~70% add, ~20% cancel, ~10% BBO. A cancel always targets a currently-live order
// (if nothing is live yet, the slot becomes an add).
inline std::vector<Op> make_workload(std::size_t n, std::uint32_t seed = 42,
                                     PriceModel model = PriceModel::Uniform,
                                     unsigned add_pct = 70, unsigned cancel_pct = 20) {
    std::mt19937 rng(seed);
    std::vector<Op> ops;
    ops.reserve(n);
    std::vector<OrderId> live;
    live.reserve(n);
    OrderId next_id = 1;  // ids are unique and never reused

    for (std::size_t i = 0; i < n; ++i) {
        const unsigned r = rng() % 100;
        if (r >= add_pct + cancel_pct) {
            ops.push_back(Op{0, 0, 0, OpType::Bbo, Side::Buy});
        } else if (r >= add_pct && !live.empty()) {
            const std::size_t k = rng() % live.size();
            const OrderId id = live[k];
            live[k] = live.back();  // swap-remove
            live.pop_back();
            ops.push_back(Op{id, 0, 0, OpType::Cancel, Side::Buy});
        } else {
            const Side side = (rng() & 1u) ? Side::Buy : Side::Sell;
            const long qty = 1 + static_cast<long>(rng() % 100);
            int offset;
            if (model == PriceModel::Uniform) {
                offset = static_cast<int>(rng() % kLevelsPerSide);
            } else {
                offset = 0;  // geometric: each step outward with probability 0.96 (mean ~24 ticks)
                while (offset < kLevelsPerSide - 1 && rng() % 100 < 96) ++offset;
            }
            Price px;
            if (model == PriceModel::Uniform)
                px = (side == Side::Buy) ? kBidLoTick + offset : kAskLoTick + offset;
            else
                px = (side == Side::Buy) ? kBidHiTick - offset : kAskLoTick + offset;
            const OrderId id = next_id++;
            live.push_back(id);
            ops.push_back(Op{id, qty, px, OpType::Add, side});
        }
    }
    return ops;
}

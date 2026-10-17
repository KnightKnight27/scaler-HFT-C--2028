#pragma once
// Random workload shared by the benchmark and --verify.
// The full op list is built before any timing starts, so every
// implementation gets the exact same sequence and the RNG is never timed.

#include <random>
#include <vector>

#include "order_book.h"

enum class OpType : uint8_t { Add, Cancel, Bbo };

struct Op {
    OpType type;
    Side side;      // add only
    uint64_t id;    // add / cancel
    Tick tick;      // add only
    int32_t qty;    // add only
};

struct WorkloadConfig {
    uint32_t seed = 42;
    size_t ops = 1000000;
    int add_pct = 70;
    int cancel_pct = 20;        // the rest are BBO queries
    int band = kLevelsPerSide;  // how many price levels per side to use
};

struct WorkloadStats {
    size_t adds = 0, cancels = 0, bbos = 0;
    size_t live_at_end = 0;
};

inline std::vector<Op> generate_workload(const WorkloadConfig& cfg, WorkloadStats* stats = nullptr) {
    std::mt19937 rng(cfg.seed);

    std::vector<Op> ops;
    ops.reserve(cfg.ops);

    // Live ids, and for each id its position in `live`, so a random live
    // order can be removed in O(1). Ids are handed out from 1 upwards, so a
    // plain vector indexed by id works as the position table.
    std::vector<uint64_t> live;
    std::vector<uint32_t> pos(cfg.ops + 1, 0);
    uint64_t next_id = 1;
    WorkloadStats st;

    for (size_t i = 0; i < cfg.ops; i++) {
        // rng() % n instead of uniform_int_distribution, so the sequence is
        // the same with libstdc++ and libc++. The tiny bias does not matter.
        uint32_t r = rng() % 100;
        Op op{};

        bool want_cancel = r >= (uint32_t)cfg.add_pct && r < (uint32_t)(cfg.add_pct + cfg.cancel_pct);
        if (want_cancel && !live.empty()) {
            size_t k = rng() % live.size();
            uint64_t id = live[k];
            live[k] = live.back();
            pos[live[k]] = (uint32_t)k;
            live.pop_back();
            op.type = OpType::Cancel;
            op.id = id;
            st.cancels++;
        } else if (r < (uint32_t)cfg.add_pct || want_cancel) {
            // a cancel with nothing to cancel becomes an add
            op.type = OpType::Add;
            op.side = (rng() % 2) ? Side::Sell : Side::Buy;
            op.qty = 1 + (int32_t)(rng() % 100);
            if (op.side == Side::Buy) op.tick = kMinBidTick + (Tick)(rng() % cfg.band);
            else                      op.tick = kMaxAskTick - (Tick)(rng() % cfg.band);
            op.id = next_id++;
            pos[op.id] = (uint32_t)live.size();
            live.push_back(op.id);
            st.adds++;
        } else {
            op.type = OpType::Bbo;
            st.bbos++;
        }
        ops.push_back(op);
    }
    st.live_at_end = live.size();
    if (stats) *stats = st;
    return ops;
}

#include "workload.hpp"

#include <algorithm>
#include <random>

namespace {

// Maps one 32-bit draw to [0, n). std::uniform_int_distribution is avoided on
// purpose: libstdc++ and libc++ implement it differently, so the same seed would
// give a different workload on Linux and on macOS.
std::uint32_t random_below(std::mt19937& rng, std::uint32_t n) {
    return static_cast<std::uint32_t>((static_cast<std::uint64_t>(rng()) * n) >> 32);
}

}  // namespace

Workload generate_workload(const WorkloadConfig& config) {
    std::mt19937 rng(config.seed);
    Workload workload;
    workload.ops.reserve(config.num_ops);
    std::vector<std::uint64_t> live;  // ids of resting orders, in no particular order
    std::uint64_t next_id = 1;

    auto cancel_random_live_order = [&]() {
        const std::size_t k = random_below(rng, static_cast<std::uint32_t>(live.size()));
        Op op{};
        op.type = OpType::Cancel;
        op.id = live[k];
        live[k] = live.back();  // swap with the last id so removal is O(1)
        live.pop_back();
        workload.ops.push_back(op);
        ++workload.cancels;
    };

    const int cancel_end = config.add_percent + config.cancel_percent;
    for (std::size_t i = 0; i < config.num_ops; ++i) {
        const int roll = static_cast<int>(random_below(rng, 100));
        const bool is_cancel = roll >= config.add_percent && roll < cancel_end;

        if (roll < config.add_percent || (is_cancel && live.empty())) {
            Op op{};
            op.type = OpType::Add;
            op.id = next_id++;
            op.side = random_below(rng, 2) == 0 ? Side::Buy : Side::Sell;
            const Tick lowest = op.side == Side::Buy ? kMinBidTick : kMinAskTick;
            op.price = tick_to_price(lowest + static_cast<Tick>(random_below(rng, kLevelsPerSide)));
            op.qty = 1 + random_below(rng, 100);
            workload.ops.push_back(op);
            live.push_back(op.id);
            workload.max_live_orders = std::max(workload.max_live_orders, live.size());
            ++workload.adds;
        } else if (is_cancel) {
            cancel_random_live_order();
        } else {
            Op op{};
            op.type = OpType::Bbo;
            workload.ops.push_back(op);
            ++workload.bbo_queries;
        }
    }

    if (config.cancel_all_at_end) {
        while (!live.empty()) cancel_random_live_order();
    }
    return workload;
}

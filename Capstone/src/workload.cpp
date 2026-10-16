#include "workload.h"

#include <random>

// Note: std::mt19937's output sequence is fixed by the standard, but the
// distributions (uniform_int_distribution) are implementation-defined. The same
// standard library (libstdc++ on both MinGW and Linux g++) gives the same stream;
// a different library may not. hash_workload() makes any difference visible.
namespace {

// A bijection on 64-bit integers (each step is invertible: xor with a right shift of
// itself, and multiplication by an odd constant mod 2^64). Distinct inputs therefore
// give distinct ids. Constants from the splitmix64 finaliser.
uint64_t scramble(uint64_t x) {
    x ^= x >> 30;
    x *= 0xBF58476D1CE4E5B9ull;
    x ^= x >> 27;
    x *= 0x94D049BB133111EBull;
    x ^= x >> 31;
    return x;
}

}  // namespace

std::vector<Op> generate_workload(const WorkloadSpec& spec, std::size_t* fallback_adds) {
    const std::size_t num_ops = spec.num_ops;
    const OpMix mix = spec.mix;
    std::mt19937 rng(spec.seed);
    std::uniform_int_distribution<int> percent(0, 99);
    std::uniform_int_distribution<int> coin(0, 1);
    std::uniform_int_distribution<long> qty_dist(1, 100);
    std::uniform_int_distribution<Tick> bid_tick(kMinBidTick, kMaxBidTick);
    std::uniform_int_distribution<Tick> ask_tick(kMinAskTick, kMaxAskTick);

    // Live set: ids of live orders in a vector. A cancel picks a uniformly random index
    // and removes it by swapping in the last element, so pick + remove is O(1). Nothing
    // ever looks an id up in this set, so no id -> index map is needed.
    std::vector<uint64_t> live;
    live.reserve(num_ops);

    std::vector<Op> ops;
    ops.reserve(num_ops);
    uint64_t next_seq = 1;
    std::size_t fallbacks = 0;

    for (std::size_t i = 0; i < num_ops; ++i) {
        const int roll = percent(rng);
        const bool wants_add = roll < mix.add_percent;
        const bool wants_cancel = !wants_add && roll < mix.add_percent + mix.cancel_percent;
        if (wants_cancel && live.empty()) ++fallbacks;

        if (wants_cancel && !live.empty()) {
            std::uniform_int_distribution<std::size_t> pick(0, live.size() - 1);
            const std::size_t index = pick(rng);
            const uint64_t id = live[index];
            live[index] = live.back();
            live.pop_back();
            ops.push_back(Op{OpType::Cancel, Side::Buy, id, 0.0, 0});
        } else if (!wants_add && !wants_cancel) {
            ops.push_back(Op{OpType::Bbo, Side::Buy, 0, 0.0, 0});
        } else {
            // An add, or a cancel drawn while no order is live (fallback add).
            const Side side = coin(rng) == 0 ? Side::Buy : Side::Sell;
            const Tick tick = side == Side::Buy ? bid_tick(rng) : ask_tick(rng);
            const long qty = qty_dist(rng);
            const uint64_t seq = next_seq++;
            const uint64_t id = spec.scrambled_ids ? scramble(seq) : seq;
            live.push_back(id);
            ops.push_back(Op{OpType::Add, side, id, tick_to_price(tick), qty});
        }
    }
    if (fallback_adds) *fallback_adds = fallbacks;
    return ops;
}

WorkloadStats count_ops(const std::vector<Op>& ops) {
    WorkloadStats stats;
    for (const Op& op : ops) {
        switch (op.type) {
            case OpType::Add: ++stats.adds; break;
            case OpType::Cancel: ++stats.cancels; break;
            case OpType::Bbo: ++stats.bbos; break;
        }
    }
    return stats;
}

uint64_t hash_workload(const std::vector<Op>& ops) {
    uint64_t h = 14695981039346656037ull;  // FNV-1a 64-bit offset basis
    const auto mix = [&h](uint64_t value) {
        for (int byte = 0; byte < 8; ++byte) {
            h ^= (value >> (byte * 8)) & 0xFF;
            h *= 1099511628211ull;  // FNV prime
        }
    };
    for (const Op& op : ops) {
        mix(static_cast<uint64_t>(op.type));
        mix(static_cast<uint64_t>(op.side));
        mix(op.id);
        mix(static_cast<uint64_t>(price_to_tick(op.price)));
        mix(static_cast<uint64_t>(op.qty));
    }
    return h;
}

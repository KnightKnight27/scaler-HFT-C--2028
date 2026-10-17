#pragma once
#include "types.hpp"
#include <random>
#include <string>

namespace ob {
enum class Kind : unsigned { Add, Cancel, Bbo };
struct Operation { Kind kind; Order order; };
// Specify our rejection sampler rather than implementation-dependent distributions.
inline std::uint64_t bounded(std::mt19937_64& rng, std::uint64_t bound) {
    const auto threshold = (std::uint64_t{0} - bound) % bound;
    std::uint64_t value;
    do { value = rng(); } while (value < threshold);
    return value % bound;
}
inline void mix(std::uint64_t& hash, std::uint64_t value) {
    // Stable FNV-1a, little-endian logical serialization, independent of padding.
    for (int i = 0; i < 8; ++i) { hash ^= value & 255; hash *= 1099511628211ULL; value >>= 8; }
}
inline void consume(std::uint64_t& hash, const BBO& b) {
    mix(hash, b.has_bid); mix(hash, b.bid_price); mix(hash, b.bid_qty);
    mix(hash, b.has_ask); mix(hash, b.ask_price); mix(hash, b.ask_qty);
}
inline void consume(std::uint64_t& hash, const std::vector<Level>& levels) {
    mix(hash, levels.size());
    for (const auto& l : levels) { mix(hash, l.price); mix(hash, l.qty); }
}
struct Trace {
    std::vector<Operation> ops;
    std::uint64_t hash = 14695981039346656037ULL;
    std::size_t adds = 0, cancels = 0, queries = 0, buys = 0, empty_fallbacks = 0;
};
inline Trace generate(std::size_t count, std::uint64_t seed) {
    Trace t;
    t.ops.reserve(count);
    std::vector<std::uint64_t> live;
    std::mt19937_64 rng(seed);
    std::uint64_t next_id = 0;
    for (std::size_t i = 0; i < count; ++i) {
        const auto choice = bounded(rng, 100);
        Operation op{Kind::Bbo, {0, Side::Buy, 0, 0}};
        if (choice < 70 || (choice < 90 && live.empty())) {
            if (choice >= 70) ++t.empty_fallbacks;
            op.kind = Kind::Add;
            const Side side = bounded(rng, 2) == 0 ? Side::Buy : Side::Sell;
            const int price = (side == Side::Buy ? 5000 : 10001) + static_cast<int>(bounded(rng, 5000));
            const Qty qty = 1 + static_cast<Qty>(bounded(rng, 100));
            op.order = {next_id++, side, price, qty};
            live.push_back(op.order.id);
            ++t.adds;
            t.buys += side == Side::Buy;
        } else if (choice < 90) {
            const auto index = static_cast<std::size_t>(bounded(rng, live.size()));
            op.kind = Kind::Cancel;
            op.order.id = live[index];
            live[index] = live.back();
            live.pop_back();
            ++t.cancels;
        } else ++t.queries;
        mix(t.hash, static_cast<unsigned>(op.kind)); mix(t.hash, op.order.id);
        mix(t.hash, static_cast<unsigned>(op.order.side));
        mix(t.hash, op.order.price); mix(t.hash, op.order.qty);
        t.ops.push_back(op);
    }
    return t;
}
inline std::string describe(const Operation& op) {
    return "kind=" + std::to_string(static_cast<unsigned>(op.kind)) +
           " id=" + std::to_string(op.order.id) + " side=" +
           std::to_string(static_cast<unsigned>(op.order.side)) + " price=" +
           std::to_string(op.order.price) + " qty=" + std::to_string(op.order.qty);
}
} // namespace ob

#pragma once

#include "types.hpp"
#include <algorithm>
#include <array>
#include <random>

namespace ob {
enum class Kind { Add, Cancel, Query };
struct Operation {
    Kind kind;
    OrderId id = 0;
    Side side = Side::Buy;
    Price price = 0;
    Quantity qty = 0;
};

inline void mix(std::uint64_t& hash, std::uint64_t value) {
    hash = (hash ^ value) * UINT64_C(1099511628211);
}
inline void consume(std::uint64_t& hash, const BBO& bbo) {
    mix(hash, bbo.has_bid); mix(hash, bbo.bid_price); mix(hash, bbo.bid_qty);
    mix(hash, bbo.has_ask); mix(hash, bbo.ask_price); mix(hash, bbo.ask_qty);
}
inline void consume(std::uint64_t& hash, const std::vector<Level>& levels) {
    mix(hash, levels.size());
    for (const auto& level : levels) { mix(hash, level.price); mix(hash, level.qty); }
}

struct Workload {
    std::vector<Operation> operations;
    std::array<std::size_t, 3> counts{};
    std::size_t peak_live = 0;
    std::uint64_t checksum = 0;
};

inline Workload make_workload(std::size_t count, std::uint32_t seed) {
    std::mt19937 rng(seed);
    auto random_below = [&rng](std::uint32_t bound) {
        const std::uint32_t threshold = (std::uint32_t{0} - bound) % bound;
        std::uint32_t value;
        do { value = static_cast<std::uint32_t>(rng()); } while (value < threshold);
        return value % bound;
    };
    Workload w;
    w.operations.reserve(count);
    std::vector<OrderId> live;
    live.reserve(count);
    OrderId next_id = 1;
    for (std::size_t i = 0; i < count; ++i) {
        const auto choice = random_below(100);
        Operation op{};
        if (choice < 70 || (choice < 90 && live.empty())) {
            op.kind = Kind::Add;
            op.id = next_id++;
            op.side = random_below(2) == 0 ? Side::Buy : Side::Sell;
            op.price = (op.side == Side::Buy ? 5000 : 10001) + random_below(price_count);
            op.qty = 1 + random_below(100);
            live.push_back(op.id);
            w.peak_live = std::max(w.peak_live, live.size());
        } else if (choice < 90) {
            op.kind = Kind::Cancel;
            const auto slot = random_below(static_cast<std::uint32_t>(live.size()));
            op.id = live[slot];
            live[slot] = live.back();
            live.pop_back();
        } else {
            op.kind = Kind::Query;
        }
        ++w.counts[static_cast<int>(op.kind)];
        mix(w.checksum, static_cast<int>(op.kind)); mix(w.checksum, op.id);
        mix(w.checksum, static_cast<int>(op.side)); mix(w.checksum, op.price);
        mix(w.checksum, op.qty);
        w.operations.push_back(op);
    }
    return w;
}

template<class Book>
bool apply(Book& book, const Operation& op, std::uint64_t& checksum) {
    switch (op.kind) {
    case Kind::Add: book.add_order(op.id, op.side, op.price, op.qty); return true;
    case Kind::Cancel: {
        const bool removed = book.cancel_order(op.id);
        mix(checksum, removed);
        return removed;
    }
    case Kind::Query: consume(checksum, book.get_bbo()); return true;
    }
    return false;
}

void run_edge_tests();
void verify(const Workload& workload);
}

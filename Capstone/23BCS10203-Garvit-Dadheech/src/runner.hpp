// Replays a workload through one book and times each op.  Used by bench.cpp
// and by tools/apple_pmu.cpp so both measure the same loop.
#pragma once

#include <chrono>
#include <cstdint>
#include <vector>

#include "common.hpp"
#include "workload.hpp"

namespace ob {

using Clock = std::chrono::steady_clock;

// Fold everything the book returns into one number so the compiler can't
// drop any of the calls.
inline uint64_t mix(uint64_t h, uint64_t v) {
    return h ^ (v + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2));
}

inline uint64_t fold_bbo(uint64_t h, const BBO& b) {
    h = mix(h, b.has_bid);
    h = mix(h, b.has_ask);
    if (b.has_bid) h = mix(mix(h, static_cast<uint64_t>(to_tick(b.bid_price))), static_cast<uint64_t>(b.bid_qty));
    if (b.has_ask) h = mix(mix(h, static_cast<uint64_t>(to_tick(b.ask_price))), static_cast<uint64_t>(b.ask_qty));
    return h;
}

inline uint64_t fold_levels(uint64_t h, const std::vector<Level>& ls) {
    h = mix(h, ls.size());
    for (const Level& l : ls) h = mix(mix(h, static_cast<uint64_t>(to_tick(l.price))), static_cast<uint64_t>(l.qty));
    return h;
}

struct RunResult {
    double total_ms = 0;   // wall time of the whole operation loop
    double add_ns = 0;     // mean ns per add_order
    double cancel_ns = 0;  // mean ns per cancel_order
    double bbo_ns = 0;     // mean ns per get_bbo
    double depth_ns = 0;   // mean ns per (get_bids(10) + get_asks(10))
    uint64_t checksum = 0;
    BBO final_bbo{};
};

// kTimers: time every call with a steady_clock pair.  Off under perf so only
// the whole loop is timed.
template <class Book, bool kTimers>
RunResult run_once(const std::vector<Op>& ops) {
    Book book;
    uint64_t checksum = 0;
    int64_t add_ns = 0, cancel_ns = 0, bbo_ns = 0, depth_ns = 0;
    size_t adds = 0, cancels = 0, bbos = 0, depths = 0;

    const auto t_start = Clock::now();
    for (const Op& op : ops) {
        switch (op.type) {
            case OpType::Add: {
                const auto t0 = kTimers ? Clock::now() : Clock::time_point{};
                book.add_order(op.id, op.side, to_price(op.tick), op.qty);
                if (kTimers) add_ns += (Clock::now() - t0).count();
                ++adds;
                break;
            }
            case OpType::Cancel: {
                const auto t0 = kTimers ? Clock::now() : Clock::time_point{};
                const bool ok = book.cancel_order(op.id);
                if (kTimers) cancel_ns += (Clock::now() - t0).count();
                checksum = mix(checksum, ok);
                ++cancels;
                break;
            }
            case OpType::Bbo: {
                const auto t0 = kTimers ? Clock::now() : Clock::time_point{};
                const BBO b = book.get_bbo();
                if (kTimers) bbo_ns += (Clock::now() - t0).count();
                checksum = fold_bbo(checksum, b);
                ++bbos;
                break;
            }
            case OpType::Depth: {
                const auto t0 = kTimers ? Clock::now() : Clock::time_point{};
                const std::vector<Level> bids = book.get_bids(10);
                const std::vector<Level> asks = book.get_asks(10);
                if (kTimers) depth_ns += (Clock::now() - t0).count();
                checksum = fold_levels(fold_levels(checksum, bids), asks);
                ++depths;
                break;
            }
        }
    }
    const auto t_end = Clock::now();

    RunResult r;
    r.total_ms = std::chrono::duration<double, std::milli>(t_end - t_start).count();
    if (kTimers) {
        r.add_ns = adds ? double(add_ns) / adds : 0;
        r.cancel_ns = cancels ? double(cancel_ns) / cancels : 0;
        r.bbo_ns = bbos ? double(bbo_ns) / bbos : 0;
        r.depth_ns = depths ? double(depth_ns) / depths : 0;
    }
    r.final_bbo = book.get_bbo();
    r.checksum = fold_bbo(checksum, r.final_bbo);
    return r;
}

// How much one steady_clock pair costs, printed so it can be subtracted.
inline double timer_overhead_ns() {
    constexpr int N = 200000;
    int64_t acc = 0;
    for (int i = 0; i < N; ++i) {
        const auto a = Clock::now();
        const auto b = Clock::now();
        acc += (b - a).count();
    }
    return double(acc) / N;
}

}  // namespace ob

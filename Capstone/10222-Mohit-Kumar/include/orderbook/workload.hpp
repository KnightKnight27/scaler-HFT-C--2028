#pragma once
#include "types.hpp"
#include <algorithm>
#include <random>

namespace ob {
enum class Kind { Add, Cancel, Query };
struct Event { Kind kind; std::uint64_t id; Side side; Tick price; long qty; };
struct Workload {
    std::vector<Event> events;
    std::size_t adds = 0, cancels = 0, queries = 0, peak_live = 0;
    std::uint64_t hash = 14695981039346656037ULL;
};
// Specify bounded sampling ourselves to keep the stream identical across standard libraries.
inline std::uint32_t below(std::mt19937& rng, std::uint32_t bound) {
    const auto threshold = static_cast<std::uint32_t>(-bound) % bound;
    std::uint32_t value;
    do { value = static_cast<std::uint32_t>(rng()); } while (value < threshold);
    return value % bound;
}
inline void hash_value(std::uint64_t& hash, std::uint64_t value) {
    for (int i = 0; i < 8; ++i) { hash ^= value & 255; hash *= 1099511628211ULL; value >>= 8; }
}
inline Workload generate(std::size_t count, std::uint32_t seed) {
    Workload w;
    w.events.reserve(count);
    std::vector<std::uint64_t> live;
    live.reserve(count);
    std::mt19937 rng(seed);
    std::uint64_t next_id = 1;
    for (std::size_t i = 0; i < count; ++i) {
        const auto choice = below(rng, 100);
        Event e{Kind::Query, 0, Side::Buy, 0, 0};
        if (choice < 70 || (choice < 90 && live.empty())) {
            e.kind = Kind::Add; e.id = next_id++;
            e.side = below(rng, 2) == 0 ? Side::Buy : Side::Sell;
            e.price = base_tick(e.side) + static_cast<int>(below(rng, levels_per_side));
            e.qty = 1 + static_cast<long>(below(rng, 100));
            live.push_back(e.id); ++w.adds;
            w.peak_live = std::max(w.peak_live, live.size());
        } else if (choice < 90) {
            e.kind = Kind::Cancel;
            const auto pos = below(rng, static_cast<std::uint32_t>(live.size()));
            e.id = live[pos]; live[pos] = live.back(); live.pop_back(); ++w.cancels;
        } else ++w.queries;
        w.events.push_back(e);
        hash_value(w.hash, static_cast<std::uint64_t>(e.kind)); hash_value(w.hash, e.id);
        hash_value(w.hash, static_cast<std::uint64_t>(e.side));
        hash_value(w.hash, static_cast<std::uint64_t>(e.price));
        hash_value(w.hash, static_cast<std::uint64_t>(e.qty));
    }
    return w;
}
inline std::uint64_t consume(const BBO& b) {
    return static_cast<std::uint64_t>(b.bid_price) * 3 + static_cast<std::uint64_t>(b.bid_qty) * 5 +
           static_cast<std::uint64_t>(b.ask_price) * 7 + static_cast<std::uint64_t>(b.ask_qty) * 11 +
           static_cast<unsigned>(b.has_bid) + static_cast<unsigned>(b.has_ask);
}
inline std::uint64_t consume(const std::vector<Level>& levels) {
    std::uint64_t result = levels.size();
    for (const auto& l : levels) result = result * 131 + static_cast<std::uint64_t>(l.price) +
                                        static_cast<std::uint64_t>(l.qty);
    return result;
}
template<class Book> inline std::uint64_t apply(Book& book, const Event& e) {
    switch (e.kind) {
        case Kind::Add: book.add_order(e.id, e.side, e.price, e.qty); return 0;
        case Kind::Cancel:
            if (!book.cancel_order(e.id)) throw std::logic_error("workload cancellation was not live");
            return 1;
        case Kind::Query: return consume(book.get_bbo());
    }
    throw std::logic_error("invalid workload operation");
}
} // namespace ob

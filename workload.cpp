#include "workload.hpp"
#include <random>

namespace {
// Rejection sampling makes the seeded workload independent of the library's
// uniform_int_distribution implementation and avoids modulo bias.
std::uint32_t bounded(std::mt19937 &rng, std::uint32_t bound) {
    const auto threshold = (std::uint32_t{0} - bound) % bound;
    std::uint32_t value;
    do {
        value = static_cast<std::uint32_t>(rng());
    } while (value < threshold);
    return value % bound;
}
void digest(std::uint64_t &hash, std::uint64_t value) {
    for (int byte = 0; byte < 8; ++byte) {
        hash ^= value & 255U;
        hash *= 1099511628211ULL;
        value >>= 8;
    }
}
} // namespace

Workload generate_workload(std::size_t count, std::uint32_t seed) {
    Workload out;
    out.operations.reserve(count);
    std::vector<std::uint64_t> live;
    live.reserve(count);
    std::mt19937 rng(seed);
    std::uint64_t next_id = 1;
    for (std::size_t i = 0; i < count; ++i) {
        const auto choice = bounded(rng, 100);
        Operation op{Kind::Query};
        if (choice < 70 || (choice < 90 && live.empty())) {
            op.kind = Kind::Add;
            op.id = next_id++;
            op.side = bounded(rng, 2) == 0 ? Side::Buy : Side::Sell;
            op.price_ticks =
                (op.side == Side::Buy ? 5000 : 10001) + static_cast<int>(bounded(rng, 5000));
            op.qty = 1 + static_cast<long>(bounded(rng, 100));
            live.push_back(op.id);
            ++out.adds;
        } else if (choice < 90) {
            op.kind = Kind::Cancel;
            const auto index = bounded(rng, static_cast<std::uint32_t>(live.size()));
            op.id = live[index];
            live[index] = live.back();
            live.pop_back();
            ++out.cancels;
        } else {
            ++out.queries;
        }
        digest(out.fingerprint, static_cast<std::uint64_t>(op.kind));
        digest(out.fingerprint, op.id);
        digest(out.fingerprint, static_cast<std::uint64_t>(op.side));
        digest(out.fingerprint, static_cast<std::uint64_t>(op.price_ticks));
        digest(out.fingerprint, static_cast<std::uint64_t>(op.qty));
        out.operations.push_back(op);
    }
    return out;
}

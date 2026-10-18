#include "workload.hpp"

#include <algorithm>

namespace {

Op make_add(uint64_t id, Side side, int32_t tick, long qty) {
    return Op{OpType::Add, side, 0, id, px::to_price(tick), qty};
}

Op make_cancel(uint64_t id) { return Op{OpType::Cancel, Side::Buy, 0, id, 0.0, 0}; }
Op make_bbo() { return Op{OpType::Bbo, Side::Buy, 0, 0, 0.0, 0}; }
Op make_depth(int depth) { return Op{OpType::Depth, Side::Buy, depth, 0, 0.0, 0}; }

void push(Workload& w, const Op& op) {
    w.ops.push_back(op);
    ++w.count[static_cast<int>(op.type)];
}

int32_t random_tick(std::mt19937& rng, Side side) {
    return side == Side::Buy ? px::kBidMinTick + static_cast<int32_t>(uniform(rng, px::kBidMaxTick - px::kBidMinTick + 1))
                             : px::kAskMinTick + static_cast<int32_t>(uniform(rng, px::kAskMaxTick - px::kAskMinTick + 1));
}

// Removes and returns a random element (swap with last, then pop).
uint64_t take_random(std::mt19937& rng, std::vector<uint64_t>& v) {
    const size_t i = uniform(rng, static_cast<uint32_t>(v.size()));
    const uint64_t id = v[i];
    v[i] = v.back();
    v.pop_back();
    return id;
}

}  // namespace

Workload make_benchmark_workload(size_t n, uint32_t seed) {
    std::mt19937 rng(seed);
    Workload w;
    w.name = "benchmark";
    w.ops.reserve(n + n / 100);

    std::vector<uint64_t> live;
    live.reserve(n);
    uint64_t next_id = 1;

    for (size_t i = 0; i < n; ++i) {
        const uint32_t r = uniform(rng, 100);
        if (r < 70 || (r < 90 && live.empty())) {
            const Side side = (rng() & 1) ? Side::Buy : Side::Sell;
            const int32_t tick = random_tick(rng, side);
            const long qty = 1 + static_cast<long>(uniform(rng, 100));
            push(w, make_add(next_id, side, tick, qty));
            live.push_back(next_id++);
            w.peak_live = std::max(w.peak_live, live.size());
        } else if (r < 90) {
            push(w, make_cancel(take_random(rng, live)));
        } else {
            push(w, make_bbo());
        }
        if ((i + 1) % 100 == 0) push(w, make_depth(10));
    }
    return w;
}

Workload make_stress_workload(size_t n, uint32_t seed) {
    std::mt19937 rng(seed);
    std::mt19937_64 id_rng(seed ^ 0x5eedu);
    Workload w;
    w.name = "stress";
    w.ops.reserve(n);

    std::vector<uint64_t> live;
    std::vector<uint64_t> dead;  // cancelled ids, to test double-cancel

    for (size_t i = 0; i < n; ++i) {
        const uint32_t r = uniform(rng, 100);
        if (r < 45) {
            // Mostly near the inside (within 8 ticks of the mid) so levels are thin.
            const Side side = (rng() & 1) ? Side::Buy : Side::Sell;
            int32_t tick = side == Side::Buy ? px::kBidMaxTick - static_cast<int32_t>(uniform(rng, 8))
                                             : px::kAskMinTick + static_cast<int32_t>(uniform(rng, 8));
            if (uniform(rng, 50) == 0) tick = random_tick(rng, side);  // occasional deep level
            const long qty = 1 + static_cast<long>(uniform(rng, 100));
            const uint64_t id = id_rng();
            push(w, make_add(id, side, tick, qty));
            live.push_back(id);
        } else if (r < 80 && !live.empty()) {
            const uint64_t id = take_random(rng, live);
            push(w, make_cancel(id));
            dead.push_back(id);
        } else if (r < 84) {
            push(w, make_cancel(id_rng()));  // unknown id
        } else if (r < 87 && !dead.empty()) {
            push(w, make_cancel(dead[uniform(rng, static_cast<uint32_t>(dead.size()))]));  // already cancelled
        } else if (r < 89 && !live.empty()) {
            // duplicate of a live id -> must be rejected
            push(w, make_add(live[uniform(rng, static_cast<uint32_t>(live.size()))], Side::Buy, px::kBidMaxTick, 5));
        } else if (r < 91) {
            // invalid orders: off-tick, out of band, wrong side of mid, bad qty
            static const double kBadPrices[] = {99.995, 49.99, 150.01, 100.00, 100.50, 0.0, -1.0};
            const Side side = (rng() & 1) ? Side::Buy : Side::Sell;
            Op op = make_add(id_rng(), side, px::kBidMaxTick, 10);
            if (uniform(rng, 2)) {
                op.price = kBadPrices[uniform(rng, 7)];
                if (op.price == 100.50 && side == Side::Sell) op.price = 99.0;  // bid price on ask side
            } else {
                op.price = px::to_price(random_tick(rng, side));
                op.qty = uniform(rng, 2) ? 0 : -5;
            }
            push(w, op);
        } else if (r < 96) {
            push(w, make_bbo());
        } else {
            push(w, make_depth(static_cast<int>(uniform(rng, 16)) - 1));  // depth -1..14
        }
        w.peak_live = std::max(w.peak_live, live.size());

        // Periodically drain the book completely to test the empty-book path.
        if (i % 50000 == 49999) {
            while (!live.empty()) {
                const uint64_t id = take_random(rng, live);
                push(w, make_cancel(id));
                dead.push_back(id);
            }
            push(w, make_bbo());
        }
    }
    return w;
}

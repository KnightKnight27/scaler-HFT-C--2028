#include "workload.hpp"
#include <random>

Workload gen_workload(size_t n_ops, uint32_t seed) {
    Workload w;
    w.ops.reserve(n_ops);

    std::mt19937 rng(seed);
    std::uniform_int_distribution<int> pct(0, 99);
    std::uniform_int_distribution<int> coin(0, 1);
    std::uniform_int_distribution<int> qty_d(1, 100);
    std::uniform_int_distribution<int32_t> bid_tick(BID_MIN_TICK, BID_MAX_TICK);
    std::uniform_int_distribution<int32_t> ask_tick(ASK_MIN_TICK, ASK_MAX_TICK);

    std::vector<uint64_t> live;  // ids currently resting, so cancels always hit
    live.reserve(n_ops);
    uint64_t next_id = 1;

    for (size_t i = 0; i < n_ops; ++i) {
        int r = pct(rng);
        Op op{};
        bool want_cancel = r >= 70 && r < 90;
        if (r < 70 || (want_cancel && live.empty())) {
            if (want_cancel) ++w.degraded;
            op.type = OpType::Add;
            op.side = coin(rng) == 0 ? Side::Buy : Side::Sell;
            op.id = next_id++;
            op.tick = op.side == Side::Buy ? bid_tick(rng) : ask_tick(rng);
            op.qty = qty_d(rng);
            live.push_back(op.id);
            ++w.n_add;
        } else if (want_cancel) {
            std::uniform_int_distribution<size_t> pick(0, live.size() - 1);
            size_t k = pick(rng);
            op.type = OpType::Cancel;
            op.id = live[k];
            live[k] = live.back();  // swap remove, O(1)
            live.pop_back();
            ++w.n_cancel;
        } else {
            op.type = OpType::Bbo;
            ++w.n_bbo;
        }
        w.ops.push_back(op);
    }
    return w;
}

#pragma once

// The op list used by both the benchmark and --verify, and the loop that
// plays it on a book.

#include <random>
#include <unordered_set>
#include <vector>

#include "common.hpp"

enum class OpType : uint8_t { Add, Cancel, Bbo };

struct Op {
    OpType type;
    Side side;
    long qty;
    double price;
    uint64_t id;
};

// The defaults are the benchmark workload from the assignment. The other
// settings are only used by --verify.
struct WorkloadOptions {
    int add_pct = 70;         // % adds, cancels fill up to 90%, the last 10% are bbo
    int bad_cancel_pct = 0;   // % of cancels that go to an unknown / already cancelled id
    bool random_ids = false;  // random 64 bit ids instead of 1, 2, 3, ...
    int band = 5000;          // only use this many ticks closest to the mid on each side
};

// Generates the op list up front so every implementation sees exactly the
// same sequence and the RNG isn't part of the timed loop.
// Every cancel picks an order that is live at that point (we keep our own
// list of live ids while generating). If nothing is live yet the cancel
// becomes an add.
inline std::vector<Op> make_workload(size_t n, uint32_t seed, const WorkloadOptions& opt = {}) {
    std::mt19937 rng(seed);
    std::uniform_int_distribution<int> pct(0, 99);
    std::uniform_int_distribution<int> coin(0, 1);
    std::uniform_int_distribution<long> qty_dist(1, 100);
    std::uniform_int_distribution<int> bid_dist(MAX_BID_TICK - opt.band + 1, MAX_BID_TICK);
    std::uniform_int_distribution<int> ask_dist(MIN_ASK_TICK, MIN_ASK_TICK + opt.band - 1);
    auto random_id = [&] { return (uint64_t(rng()) << 32) | rng(); };

    std::vector<Op> ops;
    ops.reserve(n);
    std::vector<uint64_t> live, dead;
    std::unordered_set<uint64_t> used;  // only for random_ids, so ids are never reused
    uint64_t next_id = 1;

    for (size_t i = 0; i < n; i++) {
        int r = pct(rng);
        Op op{};
        if (r < opt.add_pct || (r < 90 && live.empty())) {
            op.type = OpType::Add;
            op.side = coin(rng) ? Side::Buy : Side::Sell;
            int tick = op.side == Side::Buy ? bid_dist(rng) : ask_dist(rng);
            op.price = to_price(tick);
            op.qty = qty_dist(rng);
            if (opt.random_ids) {
                do {
                    op.id = random_id();
                } while (!used.insert(op.id).second);
            } else {
                op.id = next_id++;
            }
            live.push_back(op.id);
        } else if (r < 90) {
            op.type = OpType::Cancel;
            if (opt.bad_cancel_pct > 0 && pct(rng) < opt.bad_cancel_pct) {
                // an id that was already cancelled, or one that was never added
                if (!dead.empty() && coin(rng)) {
                    op.id = dead[std::uniform_int_distribution<size_t>(0, dead.size() - 1)(rng)];
                } else if (opt.random_ids) {
                    do {
                        op.id = random_id();
                    } while (used.count(op.id));
                } else {
                    op.id = next_id + 1000000 + rng() % 1000;
                }
            } else {
                size_t k = std::uniform_int_distribution<size_t>(0, live.size() - 1)(rng);
                op.id = live[k];
                live[k] = live.back();
                live.pop_back();
                dead.push_back(op.id);
            }
        } else {
            op.type = OpType::Bbo;
        }
        ops.push_back(op);
    }
    return ops;
}

struct WorkloadCounts {
    size_t add = 0, cancel = 0, bbo = 0;
};

inline WorkloadCounts count_ops(const std::vector<Op>& ops) {
    WorkloadCounts c;
    for (const Op& op : ops) {
        if (op.type == OpType::Add) c.add++;
        else if (op.type == OpType::Cancel) c.cancel++;
        else c.bbo++;
    }
    return c;
}

// ---- checksum of everything a book returns. Printed at the end of the
// benchmark so the compiler can't skip any work, and compared between books.

inline void mix(uint64_t& cs, uint64_t v) { cs = cs * 1000003ULL + v; }

inline void mix_bbo(uint64_t& cs, const BBO& b) {
    if (b.has_bid) { mix(cs, to_tick(b.bid_price)); mix(cs, b.bid_qty); }
    if (b.has_ask) { mix(cs, to_tick(b.ask_price)); mix(cs, b.ask_qty); }
}

inline void mix_levels(uint64_t& cs, const std::vector<Level>& lv) {
    for (const Level& l : lv) { mix(cs, to_tick(l.price)); mix(cs, l.qty); }
}

// Plays the workload on a book the way the assignment describes it
// (get_bids(10) and get_asks(10) after every 100 ops) and returns the checksum.
template <class Book>
uint64_t play(Book& book, const std::vector<Op>& ops) {
    uint64_t cs = 0;
    for (size_t i = 0; i < ops.size(); i++) {
        const Op& op = ops[i];
        switch (op.type) {
            case OpType::Add: book.add_order(op.id, op.side, op.price, op.qty); break;
            case OpType::Cancel: cs += book.cancel_order(op.id); break;
            case OpType::Bbo: mix_bbo(cs, book.get_bbo()); break;
        }
        if ((i + 1) % 100 == 0) {
            mix_levels(cs, book.get_bids(10));
            mix_levels(cs, book.get_asks(10));
        }
    }
    mix_bbo(cs, book.get_bbo());
    return cs;
}

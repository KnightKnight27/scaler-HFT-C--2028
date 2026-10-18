// Executes one workload operation against any book type. Shared by the benchmark
// and the correctness test so both exercise the books identically.
#pragma once

#include <cstring>

#include "workload.hpp"

// FNV-1a style running hash over every result the books return. Printing it at the
// end stops the compiler from discarding work, and equal checksums across
// implementations are an extra sign that they agree.
struct Checksum {
    uint64_t h = 1469598103934665603ull;
    void mix(uint64_t x) { h = (h ^ x) * 1099511628211ull; }
    void mix_price(double p) {
        uint64_t bits;
        std::memcpy(&bits, &p, sizeof bits);
        mix(bits);
    }
    void mix(const BBO& b) {
        mix(b.has_bid);
        if (b.has_bid) { mix_price(b.bid_price); mix(static_cast<uint64_t>(b.bid_qty)); }
        mix(b.has_ask);
        if (b.has_ask) { mix_price(b.ask_price); mix(static_cast<uint64_t>(b.ask_qty)); }
    }
    void mix(const std::vector<Level>& levels) {
        mix(levels.size());
        for (const Level& l : levels) { mix_price(l.price); mix(static_cast<uint64_t>(l.qty)); }
    }
};

template <class Book>
inline void execute(Book& book, const Op& op, Checksum& cs) {
    switch (op.type) {
        case OpType::Add:
            cs.mix(book.add_order(op.id, op.side, op.price, op.qty));
            break;
        case OpType::Cancel:
            cs.mix(book.cancel_order(op.id));
            break;
        case OpType::Bbo:
            cs.mix(book.get_bbo());
            break;
        case OpType::Depth:
            cs.mix(book.get_bids(op.depth));
            cs.mix(book.get_asks(op.depth));
            break;
    }
}

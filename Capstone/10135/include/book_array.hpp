// Implementation C (my own design): flat price-indexed arrays + a custom
// open-addressed order table.
//
// Idea: the price band is small and fixed, so I do not need a tree or a heap at
// all. I give every legal tick its own slot in a flat array and store the total
// quantity there, which makes a price level a single array index with no
// pointer chasing. The best bid/ask are kept as cached array indices and only
// repaired when the current best level empties.
//
// Orders are held in my own open-addressed hash table (linear probing with
// tombstones) instead of std::unordered_map, so this build also owns its order
// storage end to end and keeps everything in flat, cache-friendly arrays.
#ifndef OB_BOOK_ARRAY_HPP
#define OB_BOOK_ARRAY_HPP

#include "common.hpp"

class BookArray {
public:
    BookArray()
        : bid_qty_(ticks::kBidCount, 0),
          ask_qty_(ticks::kAskCount, 0),
          best_bid_(-1),
          best_ask_(ticks::kAskCount) {
        table_.assign(kInitCap, Slot{});
        mask_ = kInitCap - 1;
        used_ = 0;
    }

    const char* name() const { return "C (flat arrays + open-addressed orders)"; }

    void add_order(uint64_t id, Side side, double price, long qty) {
        int tick = price_to_tick(price);
        validate_new_order(side, tick, qty);

        table_insert(id, OrderRec{side, tick, qty});

        if (side == Side::Buy) {
            int idx = tick - ticks::kBidMin;
            bid_qty_[idx] += qty;
            if (idx > best_bid_) {
                best_bid_ = idx;           // new, better bid level
            }
        } else {
            int idx = tick - ticks::kAskMin;
            ask_qty_[idx] += qty;
            if (idx < best_ask_) {
                best_ask_ = idx;
            }
        }
    }

    bool cancel_order(uint64_t id) {
        OrderRec o;
        if (!table_erase(id, o)) {
            return false;
        }
        if (o.side == Side::Buy) {
            int idx = o.tick - ticks::kBidMin;
            bid_qty_[idx] -= o.qty;
            if (bid_qty_[idx] == 0 && idx == best_bid_) {
                repair_best_bid();
            }
        } else {
            int idx = o.tick - ticks::kAskMin;
            ask_qty_[idx] -= o.qty;
            if (ask_qty_[idx] == 0 && idx == best_ask_) {
                repair_best_ask();
            }
        }
        return true;
    }

    BBO get_bbo() const {
        BBO bbo;
        if (best_bid_ >= 0) {
            bbo.has_bid = true;
            bbo.bid_price = tick_to_price(best_bid_ + ticks::kBidMin);
            bbo.bid_qty = bid_qty_[best_bid_];
        }
        if (best_ask_ < ticks::kAskCount) {
            bbo.has_ask = true;
            bbo.ask_price = tick_to_price(best_ask_ + ticks::kAskMin);
            bbo.ask_qty = ask_qty_[best_ask_];
        }
        return bbo;
    }

    std::vector<Level> get_bids(int depth = 10) const {
        std::vector<Level> out;
        out.reserve(depth);
        for (int idx = best_bid_; idx >= 0 && (int)out.size() < depth; --idx) {
            if (bid_qty_[idx] != 0) {
                out.push_back({tick_to_price(idx + ticks::kBidMin), bid_qty_[idx]});
            }
        }
        return out;
    }

    std::vector<Level> get_asks(int depth = 10) const {
        std::vector<Level> out;
        out.reserve(depth);
        for (int idx = best_ask_; idx < ticks::kAskCount && (int)out.size() < depth; ++idx) {
            if (ask_qty_[idx] != 0) {
                out.push_back({tick_to_price(idx + ticks::kAskMin), ask_qty_[idx]});
            }
        }
        return out;
    }

private:
    // --- cached best repair -------------------------------------------------
    // Walk down/up to the next non-empty level. Amortised cheap because the best
    // price usually moves by one tick at a time.
    void repair_best_bid() {
        while (best_bid_ >= 0 && bid_qty_[best_bid_] == 0) {
            --best_bid_;
        }
    }
    void repair_best_ask() {
        while (best_ask_ < ticks::kAskCount && ask_qty_[best_ask_] == 0) {
            ++best_ask_;
        }
    }

    // --- open-addressed order table ----------------------------------------
    enum : uint8_t { kEmpty = 0, kFull = 1, kDead = 2 };
    struct Slot {
        uint64_t id = 0;
        OrderRec rec{};
        uint8_t state = kEmpty;
    };

    static constexpr size_t kInitCap = 1 << 16;   // power of two

    static size_t hash_id(uint64_t id) {
        // splitmix64 finaliser: cheap, spreads sequential ids well.
        id += 0x9e3779b97f4a7c15ULL;
        id = (id ^ (id >> 30)) * 0xbf58476d1ce4e5b9ULL;
        id = (id ^ (id >> 27)) * 0x94d049bb133111ebULL;
        return static_cast<size_t>(id ^ (id >> 31));
    }

    void table_insert(uint64_t id, const OrderRec& rec) {
        if ((used_ + 1) * 10 >= (mask_ + 1) * 7) {   // keep load factor under 0.7
            grow();
        }
        size_t pos = hash_id(id) & mask_;
        while (table_[pos].state == kFull) {
            if (table_[pos].id == id) {
                throw std::invalid_argument("duplicate order id");
            }
            pos = (pos + 1) & mask_;
        }
        table_[pos].id = id;
        table_[pos].rec = rec;
        table_[pos].state = kFull;
        ++used_;
    }

    bool table_erase(uint64_t id, OrderRec& out) {
        size_t pos = hash_id(id) & mask_;
        while (table_[pos].state != kEmpty) {
            if (table_[pos].state == kFull && table_[pos].id == id) {
                out = table_[pos].rec;
                table_[pos].state = kDead;   // tombstone keeps probe chains intact
                --used_;
                return true;
            }
            pos = (pos + 1) & mask_;
        }
        return false;
    }

    void grow() {
        std::vector<Slot> old = std::move(table_);
        size_t new_cap = (mask_ + 1) * 2;
        table_.assign(new_cap, Slot{});
        mask_ = new_cap - 1;
        used_ = 0;
        for (const Slot& s : old) {
            if (s.state == kFull) {
                size_t pos = hash_id(s.id) & mask_;
                while (table_[pos].state == kFull) {
                    pos = (pos + 1) & mask_;
                }
                table_[pos] = s;
                table_[pos].state = kFull;
                ++used_;
            }
        }
    }

    std::vector<long> bid_qty_;   // index = tick - kBidMin
    std::vector<long> ask_qty_;   // index = tick - kAskMin
    int best_bid_;                // highest active bid index, -1 if none
    int best_ask_;                // lowest active ask index, kAskCount if none

    std::vector<Slot> table_;
    size_t mask_;
    size_t used_;
};

#endif // OB_BOOK_ARRAY_HPP

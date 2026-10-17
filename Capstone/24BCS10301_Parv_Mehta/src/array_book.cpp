#include "array_book.h"

#include <cstring>

// Index of the highest set bit in a non-zero word.
static int high_bit(uint64_t x) { return 63 - __builtin_clzll(x); }

ArrayBook::ArrayBook(size_t expected_orders) : orders_(expected_orders) {}

void ArrayBook::add_order(uint64_t id, Side side, double price, long qty) {
    Tick tick = to_tick(price);
    if (qty <= 0 || !tick_in_band(side, tick)) return;
    if (!orders_.insert(id, tick, (int32_t)qty)) return;  // duplicate id

    if (side == Side::Buy) bids_.add(tick - kMinBidTick, (int32_t)qty);
    else                   asks_.add(kMaxAskTick - tick, (int32_t)qty);
}

bool ArrayBook::cancel_order(uint64_t id) {
    int32_t tick, qty;
    if (!orders_.erase(id, tick, qty)) return false;

    if (is_bid_tick(tick)) bids_.remove(tick - kMinBidTick, qty);
    else                   asks_.remove(kMaxAskTick - tick, qty);
    return true;
}

BBO ArrayBook::get_bbo() const {
    BBO b{};
    if (bids_.best >= 0) {
        b.has_bid = true;
        b.bid_price = to_price(bid_tick(bids_.best));
        b.bid_qty = bids_.qty[bids_.best];
    }
    if (asks_.best >= 0) {
        b.has_ask = true;
        b.ask_price = to_price(ask_tick(asks_.best));
        b.ask_qty = asks_.qty[asks_.best];
    }
    return b;
}

std::vector<Level> ArrayBook::get_bids(int depth) const {
    std::vector<Level> out;
    for (int i = bids_.best; i >= 0 && (int)out.size() < depth; i = bids_.highest_below(i))
        out.push_back(Level{to_price(bid_tick(i)), bids_.qty[i]});
    return out;
}

std::vector<Level> ArrayBook::get_asks(int depth) const {
    std::vector<Level> out;
    for (int i = asks_.best; i >= 0 && (int)out.size() < depth; i = asks_.highest_below(i))
        out.push_back(Level{to_price(ask_tick(i)), asks_.qty[i]});
    return out;
}

// ---- SideLevels ----

ArrayBook::SideLevels::SideLevels() : best(-1) {
    std::memset(qty, 0, sizeof(qty));
    std::memset(occ, 0, sizeof(occ));
    std::memset(summary, 0, sizeof(summary));
}

void ArrayBook::SideLevels::add(int i, int32_t q) {
    if (qty[i] == 0) {
        int w = i >> 6;
        occ[w] |= 1ull << (i & 63);
        summary[w >> 6] |= 1ull << (w & 63);
    }
    qty[i] += q;
    if (i > best) best = i;
}

void ArrayBook::SideLevels::remove(int i, int32_t q) {
    qty[i] -= q;
    if (qty[i] == 0) {
        int w = i >> 6;
        occ[w] &= ~(1ull << (i & 63));
        if (occ[w] == 0) summary[w >> 6] &= ~(1ull << (w & 63));
        if (i == best) best = highest_below(i);
    }
}

// Highest occupied index below i, or -1 if there is none.
int ArrayBook::SideLevels::highest_below(int i) const {
    int w = i >> 6;
    uint64_t bits = occ[w] & ((1ull << (i & 63)) - 1);  // bits below i in this word
    if (bits) return (w << 6) + high_bit(bits);

    // This word is empty below i. Use the summary to find the next
    // non-empty word further down.
    int sw = w >> 6;
    uint64_t sbits = summary[sw] & ((1ull << (w & 63)) - 1);
    while (true) {
        if (sbits) {
            w = (sw << 6) + high_bit(sbits);
            return (w << 6) + high_bit(occ[w]);
        }
        sw--;
        if (sw < 0) return -1;
        sbits = summary[sw];
    }
}

// ---- OrderTable ----

ArrayBook::OrderTable::OrderTable(size_t expected) {
    size_t cap = 64;
    while (cap < expected * 2) cap *= 2;
    init(cap);
}

void ArrayBook::OrderTable::init(size_t cap) {
    slots_.assign(cap, Slot{kEmpty, 0, 0});
    mask_ = cap - 1;
    shift_ = 64 - __builtin_ctzll(cap);
}

// Fibonacci hashing: multiply by the golden ratio and keep the top bits.
size_t ArrayBook::OrderTable::home(uint64_t id) const {
    return (size_t)((id * 0x9E3779B97F4A7C15ull) >> shift_);
}

bool ArrayBook::OrderTable::insert(uint64_t id, int32_t tick, int32_t qty) {
    if (id == kEmpty) return false;
    if ((size_ + 1) * 4 > slots_.size() * 3) grow();  // keep load under 75%

    size_t i = home(id);
    while (slots_[i].id != kEmpty) {
        if (slots_[i].id == id) return false;
        i = (i + 1) & mask_;
    }
    slots_[i] = Slot{id, qty, tick};
    size_++;
    return true;
}

// Remove the order and return its tick and qty through the references.
bool ArrayBook::OrderTable::erase(uint64_t id, int32_t& tick, int32_t& qty) {
    size_t i = home(id);
    while (slots_[i].id != id) {
        if (slots_[i].id == kEmpty) return false;
        i = (i + 1) & mask_;
    }
    tick = slots_[i].tick;
    qty = slots_[i].qty;

    // Backward shift: pull later entries of the same probe run into the
    // hole so lookups never have to skip over dead slots.
    size_t hole = i;
    size_t j = (hole + 1) & mask_;
    while (slots_[j].id != kEmpty) {
        size_t h = home(slots_[j].id);
        bool movable;
        if (j > hole) movable = (h <= hole || h > j);
        else          movable = (h <= hole && h > j);
        if (movable) {
            slots_[hole] = slots_[j];
            hole = j;
        }
        j = (j + 1) & mask_;
    }
    slots_[hole].id = kEmpty;
    size_--;
    return true;
}

void ArrayBook::OrderTable::grow() {
    std::vector<Slot> old;
    old.swap(slots_);
    init(old.size() * 2);
    size_ = 0;
    for (size_t k = 0; k < old.size(); k++)
        if (old[k].id != kEmpty) insert(old[k].id, old[k].tick, old[k].qty);
}

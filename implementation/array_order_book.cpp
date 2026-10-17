#include "array_order_book.hpp"
#include <algorithm>
#include <utility>

namespace {
// Portable C++17 bit searches; the argument must be nonzero.
int lowest_bit(std::uint64_t value) {
    int bit = 0;
    if ((value & 0xffffffffULL) == 0) {
        bit += 32;
        value >>= 32;
    }
    if ((value & 0xffffULL) == 0) {
        bit += 16;
        value >>= 16;
    }
    if ((value & 0xffULL) == 0) {
        bit += 8;
        value >>= 8;
    }
    if ((value & 0xfULL) == 0) {
        bit += 4;
        value >>= 4;
    }
    if ((value & 0x3ULL) == 0) {
        bit += 2;
        value >>= 2;
    }
    if ((value & 0x1ULL) == 0) {
        ++bit;
    }
    return bit;
}
int highest_bit(std::uint64_t value) {
    int bit = 0;
    if (value >> 32) {
        bit += 32;
        value >>= 32;
    }
    if (value >> 16) {
        bit += 16;
        value >>= 16;
    }
    if (value >> 8) {
        bit += 8;
        value >>= 8;
    }
    if (value >> 4) {
        bit += 4;
        value >>= 4;
    }
    if (value >> 2) {
        bit += 2;
        value >>= 2;
    }
    if (value >> 1) {
        ++bit;
    }
    return bit;
}
} // namespace

int ArrayOrderBook::PriceSide::next(int from, bool buy) const {
    if (from < 0 || from >= prices) {
        return -1;
    }
    int word = from / 64;
    const int bit = from % 64;
    const auto all = ~std::uint64_t{0};
    // Neither expression shifts by 64, including the word boundaries.
    auto mask = buy ? (all >> (63 - bit)) : (all << bit);
    for (; word >= 0 && word < words; word += buy ? -1 : 1) {
        const auto candidates = occupied[static_cast<std::size_t>(word)] & mask;
        if (candidates) {
            return word * 64 + (buy ? highest_bit(candidates) : lowest_bit(candidates));
        }
        mask = all;
    }
    return -1;
}

void ArrayOrderBook::PriceSide::add(int index, long amount, bool buy) {
    if (qty[static_cast<std::size_t>(index)] == 0) {
        occupied[static_cast<std::size_t>(index / 64)] |= std::uint64_t{1} << (index % 64);
        ++active;
    }
    qty[static_cast<std::size_t>(index)] += amount;
    if (best == -1 || (buy ? index > best : index < best)) {
        best = index;
    }
}

void ArrayOrderBook::PriceSide::subtract(int index, long amount, bool buy) {
    auto &total = qty[static_cast<std::size_t>(index)];
    total -= amount;
    if (total == 0) {
        occupied[static_cast<std::size_t>(index / 64)] &= ~(std::uint64_t{1} << (index % 64));
        --active;
        if (best == index) {
            best = next(index, buy);
        }
    }
}

std::uint64_t ArrayOrderBook::hash(std::uint64_t value) {
    // Mix all ID bits before masking into a power-of-two table.
    value ^= value >> 30;
    value *= 0xbf58476d1ce4e5b9ULL;
    value ^= value >> 27;
    value *= 0x94d049bb133111ebULL;
    return value ^ (value >> 31);
}

std::size_t ArrayOrderBook::locate(std::uint64_t id) const {
    const auto mask = slots_.size() - 1;
    auto index = static_cast<std::size_t>(hash(id)) & mask;
    while (slots_[index].state != 0) {
        if (slots_[index].state == 1 && slots_[index].id == id) {
            return index;
        }
        index = (index + 1) & mask;
    }
    return slots_.size();
}

void ArrayOrderBook::insert(std::uint64_t id, const Order &order) {
    const auto mask = slots_.size() - 1;
    auto index = static_cast<std::size_t>(hash(id)) & mask;
    while (slots_[index].state == 1) {
        index = (index + 1) & mask;
    }
    if (slots_[index].state == 0) {
        ++used_;
    }
    slots_[index] = Slot{id, order, 1};
    ++live_;
}

void ArrayOrderBook::rebuild(std::size_t capacity) {
    auto old = std::move(slots_);
    slots_ = std::vector<Slot>(capacity);
    live_ = 0;
    used_ = 0;
    for (const auto &slot : old) {
        if (slot.state == 1) {
            insert(slot.id, slot.order);
        }
    }
}

void ArrayOrderBook::add_order(std::uint64_t id, Side side, int price_ticks, long qty) {
    if ((used_ + 1) * 10 >= slots_.size() * 7) {
        // Reclaim tombstones without growing when live occupancy permits it.
        rebuild((live_ + 1) * 10 < slots_.size() * 5 ? slots_.size() : slots_.size() * 2);
    }
    insert(id, Order{side, price_ticks, qty});
    const bool buy = side == Side::Buy;
    (buy ? bids_ : asks_).add(price_ticks - (buy ? 5000 : 10001), qty, buy);
}

bool ArrayOrderBook::cancel_order(std::uint64_t id) {
    const auto index = locate(id);
    if (index == slots_.size()) {
        return false;
    }
    const auto order = slots_[index].order;
    slots_[index].state = 2;
    --live_;
    const bool buy = order.side == Side::Buy;
    (buy ? bids_ : asks_).subtract(order.price_ticks - (buy ? 5000 : 10001), order.qty, buy);
    return true;
}

BBO ArrayOrderBook::get_bbo() const {
    BBO bbo;
    if (bids_.best != -1) {
        bbo.has_bid = true;
        bbo.bid_price_ticks = bids_.best + 5000;
        bbo.bid_qty = bids_.qty[static_cast<std::size_t>(bids_.best)];
    }
    if (asks_.best != -1) {
        bbo.has_ask = true;
        bbo.ask_price_ticks = asks_.best + 10001;
        bbo.ask_qty = asks_.qty[static_cast<std::size_t>(asks_.best)];
    }
    return bbo;
}

std::vector<Level> ArrayOrderBook::depth(const PriceSide &side, int count, bool buy) {
    std::vector<Level> out;
    if (count <= 0) {
        return out;
    }
    out.reserve(std::min(side.active, static_cast<std::size_t>(count)));
    for (int index = side.best; index != -1 && out.size() < static_cast<std::size_t>(count);
         index = side.next(index + (buy ? -1 : 1), buy)) {
        out.push_back({index + (buy ? 5000 : 10001), side.qty[static_cast<std::size_t>(index)]});
    }
    return out;
}

std::vector<Level> ArrayOrderBook::get_bids(int count) const {
    return depth(bids_, count, true);
}
std::vector<Level> ArrayOrderBook::get_asks(int count) const {
    return depth(asks_, count, false);
}

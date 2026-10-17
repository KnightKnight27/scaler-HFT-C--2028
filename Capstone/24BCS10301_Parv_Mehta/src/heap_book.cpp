#include "heap_book.h"

void HeapBook::add_order(uint64_t id, Side side, double price, long qty) {
    Tick tick = to_tick(price);
    if (qty <= 0 || !tick_in_band(side, tick)) return;

    auto ins = index_.emplace(id, tick);
    if (!ins.second) return;  // duplicate id

    if (side == Side::Buy) add_impl(bids_, tick, id, qty);
    else                   add_impl(asks_, tick, id, qty);
}

bool HeapBook::cancel_order(uint64_t id) {
    auto it = index_.find(id);
    if (it == index_.end()) return false;

    Tick tick = it->second;
    index_.erase(it);

    if (is_bid_tick(tick)) remove_impl(bids_, tick, id);
    else                   remove_impl(asks_, tick, id);
    return true;
}

BBO HeapBook::get_bbo() const {
    BBO b{};
    if (!bids_.heap.empty()) {
        Tick t = bids_.heap.front();
        b.has_bid = true;
        b.bid_price = to_price(t);
        b.bid_qty = bids_.levels.find(t)->second.qty;
    }
    if (!asks_.heap.empty()) {
        Tick t = asks_.heap.front();
        b.has_ask = true;
        b.ask_price = to_price(t);
        b.ask_qty = asks_.levels.find(t)->second.qty;
    }
    return b;
}

std::vector<Level> HeapBook::get_bids(int depth) const { return top_levels(bids_, depth); }
std::vector<Level> HeapBook::get_asks(int depth) const { return top_levels(asks_, depth); }

template <class SB>
void HeapBook::add_impl(SB& sb, Tick tick, uint64_t id, long qty) {
    LevelInfo& lv = sb.levels[tick];
    lv.qty += qty;
    lv.orders.push_back(OrderEntry{id, qty});
    if (!lv.in_heap) {
        lv.in_heap = true;
        sb.push(tick);
    }
}

template <class SB>
void HeapBook::remove_impl(SB& sb, Tick tick, uint64_t id) {
    LevelInfo& lv = sb.levels.find(tick)->second;
    std::vector<OrderEntry>& v = lv.orders;
    for (size_t i = 0; i < v.size(); i++) {
        if (v[i].id == id) {
            lv.qty -= v[i].qty;
            v[i] = v.back();
            v.pop_back();
            break;
        }
    }
    sb.pop_empty_top();
}

// A heap only exposes its top, so to list the best levels we copy it and
// pop from the copy. Empty levels are skipped.
template <class SB>
std::vector<Level> HeapBook::top_levels(const SB& sb, int depth) {
    std::vector<Level> out;
    if (depth <= 0 || sb.heap.empty()) return out;

    std::vector<Tick> h = sb.heap;
    while (!h.empty() && (int)out.size() < depth) {
        std::pop_heap(h.begin(), h.end(), sb.cmp);
        Tick t = h.back();
        h.pop_back();
        long qty = sb.levels.find(t)->second.qty;
        if (qty > 0) out.push_back(Level{to_price(t), qty});
    }
    return out;
}

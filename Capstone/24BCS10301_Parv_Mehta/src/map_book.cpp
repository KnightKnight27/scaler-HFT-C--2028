#include "map_book.h"

void MapBook::add_order(uint64_t id, Side side, double price, long qty) {
    Tick tick = to_tick(price);
    if (qty <= 0 || !tick_in_band(side, tick)) return;

    auto ins = orders_.emplace(id, Order{side, tick, qty});
    if (!ins.second) return;  // duplicate id

    if (side == Side::Buy) add_to_level(bids_, tick, qty);
    else                   add_to_level(asks_, tick, qty);
}

bool MapBook::cancel_order(uint64_t id) {
    auto it = orders_.find(id);
    if (it == orders_.end()) return false;

    Order o = it->second;
    orders_.erase(it);

    if (o.side == Side::Buy) remove_from_level(bids_, o.tick, o.qty);
    else                     remove_from_level(asks_, o.tick, o.qty);
    return true;
}

BBO MapBook::get_bbo() const {
    BBO b{};
    if (!bids_.empty()) {
        auto it = bids_.begin();
        b.has_bid = true;
        b.bid_price = to_price(it->first);
        b.bid_qty = it->second.qty;
    }
    if (!asks_.empty()) {
        auto it = asks_.begin();
        b.has_ask = true;
        b.ask_price = to_price(it->first);
        b.ask_qty = it->second.qty;
    }
    return b;
}

std::vector<Level> MapBook::get_bids(int depth) const { return top_levels(bids_, depth); }
std::vector<Level> MapBook::get_asks(int depth) const { return top_levels(asks_, depth); }

template <class M>
void MapBook::add_to_level(M& m, Tick tick, long qty) {
    LevelInfo& lv = m[tick];
    lv.qty += qty;
    lv.orders += 1;
}

template <class M>
void MapBook::remove_from_level(M& m, Tick tick, long qty) {
    auto it = m.find(tick);
    it->second.qty -= qty;
    it->second.orders -= 1;
    if (it->second.orders == 0) m.erase(it);
}

template <class M>
std::vector<Level> MapBook::top_levels(const M& m, int depth) {
    std::vector<Level> out;
    for (auto it = m.begin(); it != m.end() && (int)out.size() < depth; ++it)
        out.push_back(Level{to_price(it->first), it->second.qty});
    return out;
}

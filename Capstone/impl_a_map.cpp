#include "impl_a_map.h"

void OrderBookA::add_order(uint64_t id, Side side, double price, long qty) {
    int tick = price_to_tick(price);
    orders[id] = OrderRec{ side, tick, qty };
    if (side == Side::Buy)
        bids[tick] += qty;
    else
        asks[tick] += qty;
}

bool OrderBookA::cancel_order(uint64_t id) {
    auto it = orders.find(id);
    if (it == orders.end())
        return false;

    OrderRec rec = it->second;
    if (rec.side == Side::Buy) {
        auto lv = bids.find(rec.tick);
        lv->second -= rec.qty;
        if (lv->second == 0)
            bids.erase(lv);
    } else {
        auto lv = asks.find(rec.tick);
        lv->second -= rec.qty;
        if (lv->second == 0)
            asks.erase(lv);
    }
    orders.erase(it);
    return true;
}

BBO OrderBookA::get_bbo() const {
    BBO out{};
    if (!bids.empty()) {
        auto it = bids.begin();
        out.has_bid = true;
        out.bid_price = tick_to_price(it->first);
        out.bid_qty = it->second;
    }
    if (!asks.empty()) {
        auto it = asks.begin();
        out.has_ask = true;
        out.ask_price = tick_to_price(it->first);
        out.ask_qty = it->second;
    }
    return out;
}

std::vector<Level> OrderBookA::get_bids(int depth) const {
    std::vector<Level> out;
    int c = 0;
    for (auto it = bids.begin(); it != bids.end() && c < depth; ++it, ++c)
        out.push_back(Level{ tick_to_price(it->first), it->second });
    return out;
}

std::vector<Level> OrderBookA::get_asks(int depth) const {
    std::vector<Level> out;
    int c = 0;
    for (auto it = asks.begin(); it != asks.end() && c < depth; ++it, ++c)
        out.push_back(Level{ tick_to_price(it->first), it->second });
    return out;
}

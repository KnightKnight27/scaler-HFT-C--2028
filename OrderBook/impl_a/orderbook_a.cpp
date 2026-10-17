#include "orderbook_a.h"
#include <algorithm>

void OrderBookA::add_order(uint64_t id, Side side, double price, long qty) {
    long tick_price = price_to_ticks(price);
    m_orders[id] = OrderInfo{side, tick_price, qty};
    if (side == Side::Buy) {
        m_bids[tick_price] += qty;
    } else {
        m_asks[tick_price] += qty;
    }
}

bool OrderBookA::cancel_order(uint64_t id) {
    if (auto it = m_orders.find(id); it != m_orders.end()) {
        const OrderInfo& info = it->second;
        if (info.side == Side::Buy) {
            if (auto map_it = m_bids.find(info.price); map_it != m_bids.end()) {
                map_it->second -= info.qty;
                if (map_it->second <= 0) {
                    m_bids.erase(map_it);
                }
            }
        } else {
            if (auto map_it = m_asks.find(info.price); map_it != m_asks.end()) {
                map_it->second -= info.qty;
                if (map_it->second <= 0) {
                    m_asks.erase(map_it);
                }
            }
        }
        m_orders.erase(it);
        return true;
    }
    return false;
}

BBO OrderBookA::get_bbo() const {
    BBO bbo;
    if (!m_bids.empty()) {
        bbo.has_bid = true;
        bbo.bid_level = Level{ticks_to_price(m_bids.begin()->first), m_bids.begin()->second};
    }
    if (!m_asks.empty()) {
        bbo.has_ask = true;
        bbo.ask_level = Level{ticks_to_price(m_asks.begin()->first), m_asks.begin()->second};
    }
    return bbo;
}

std::vector<Level> OrderBookA::get_bids(int depth) const {
    std::vector<Level> result;
    if (depth <= 0) return result;
    result.reserve(std::min(static_cast<size_t>(depth), m_bids.size()));
    for (auto it = m_bids.begin(); it != m_bids.end() && result.size() < static_cast<size_t>(depth); ++it) {
        result.push_back(Level{ticks_to_price(it->first), it->second});
    }
    return result;
}

std::vector<Level> OrderBookA::get_asks(int depth) const {
    std::vector<Level> result;
    if (depth <= 0) return result;
    result.reserve(std::min(static_cast<size_t>(depth), m_asks.size()));
    for (auto it = m_asks.begin(); it != m_asks.end() && result.size() < static_cast<size_t>(depth); ++it) {
        result.push_back(Level{ticks_to_price(it->first), it->second});
    }
    return result;
}

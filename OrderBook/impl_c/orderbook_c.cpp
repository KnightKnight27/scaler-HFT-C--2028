#include "orderbook_c.h"

OrderBookC::OrderBookC() = default;

void OrderBookC::clear() {
    for (auto& pair : m_orders) {
        delete pair.second;
    }
    m_orders.clear();
    m_bids.clear();
    m_asks.clear();
}

OrderBookC::~OrderBookC() {
    clear();
}

OrderBookC::OrderBookC(OrderBookC&& other) noexcept
    : m_orders(std::move(other.m_orders)),
      m_bids(std::move(other.m_bids)),
      m_asks(std::move(other.m_asks)) {}

OrderBookC& OrderBookC::operator=(OrderBookC&& other) noexcept {
    if (this != &other) {
        clear();
        m_orders = std::move(other.m_orders);
        m_bids = std::move(other.m_bids);
        m_asks = std::move(other.m_asks);
    }
    return *this;
}

void OrderBookC::add_order(uint64_t id, Side side, double price, long qty) {
    long tick_price = price_to_ticks(price);
    Order* node = new Order{id, side, tick_price, qty, {}};
    m_orders[id] = node;

    if (side == Side::Buy) {
        // Descending order: highest price first
        auto it = m_bids.begin();
        while (it != m_bids.end() && (*it)->price > tick_price) {
            ++it;
        }
        node->list_it = m_bids.insert(it, node);
    } else {
        // Ascending order: lowest price first
        auto it = m_asks.begin();
        while (it != m_asks.end() && (*it)->price < tick_price) {
            ++it;
        }
        node->list_it = m_asks.insert(it, node);
    }
}

bool OrderBookC::cancel_order(uint64_t id) {
    if (auto it = m_orders.find(id); it != m_orders.end()) {
        Order* node = it->second;
        if (node->side == Side::Buy) {
            m_bids.erase(node->list_it);
        } else {
            m_asks.erase(node->list_it);
        }
        m_orders.erase(it);
        delete node;
        return true;
    }
    return false;
}

BBO OrderBookC::get_bbo() const {
    BBO bbo;
    if (!m_bids.empty()) {
        bbo.has_bid = true;
        long best_price = m_bids.front()->price;
        long best_qty = 0;
        for (auto it = m_bids.begin(); it != m_bids.end() && (*it)->price == best_price; ++it) {
            best_qty += (*it)->qty;
        }
        bbo.bid_level = Level{ticks_to_price(best_price), best_qty};
    }
    if (!m_asks.empty()) {
        bbo.has_ask = true;
        long best_price = m_asks.front()->price;
        long best_qty = 0;
        for (auto it = m_asks.begin(); it != m_asks.end() && (*it)->price == best_price; ++it) {
            best_qty += (*it)->qty;
        }
        bbo.ask_level = Level{ticks_to_price(best_price), best_qty};
    }
    return bbo;
}

std::vector<Level> OrderBookC::get_bids(int depth) const {
    std::vector<Level> result;
    if (depth <= 0 || m_bids.empty()) return result;
    auto it = m_bids.begin();
    while (it != m_bids.end() && result.size() < static_cast<size_t>(depth)) {
        long current_price = (*it)->price;
        long level_qty = 0;
        while (it != m_bids.end() && (*it)->price == current_price) {
            level_qty += (*it)->qty;
            ++it;
        }
        result.push_back(Level{ticks_to_price(current_price), level_qty});
    }
    return result;
}

std::vector<Level> OrderBookC::get_asks(int depth) const {
    std::vector<Level> result;
    if (depth <= 0 || m_asks.empty()) return result;
    auto it = m_asks.begin();
    while (it != m_asks.end() && result.size() < static_cast<size_t>(depth)) {
        long current_price = (*it)->price;
        long level_qty = 0;
        while (it != m_asks.end() && (*it)->price == current_price) {
            level_qty += (*it)->qty;
            ++it;
        }
        result.push_back(Level{ticks_to_price(current_price), level_qty});
    }
    return result;
}

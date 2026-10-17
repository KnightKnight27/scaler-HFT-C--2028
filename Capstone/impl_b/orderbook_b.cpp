#include "orderbook_b.h"

OrderBookB::OrderBookB() = default;

void OrderBookB::clear() {
    for (auto& pair : m_priceOrderMap) {
        delete pair.second;
    }
    m_priceOrderMap.clear();
    while (!m_bids.empty()) m_bids.pop();
    while (!m_asks.empty()) m_asks.pop();
    m_orders.clear();
}

OrderBookB::~OrderBookB() {
    clear();
}

OrderBookB::OrderBookB(OrderBookB&& other) noexcept
    : m_bids(std::move(other.m_bids)),
      m_asks(std::move(other.m_asks)),
      m_orders(std::move(other.m_orders)),
      m_priceOrderMap(std::move(other.m_priceOrderMap)) {}

OrderBookB& OrderBookB::operator=(OrderBookB&& other) noexcept {
    if (this != &other) {
        clear();
        m_bids = std::move(other.m_bids);
        m_asks = std::move(other.m_asks);
        m_orders = std::move(other.m_orders);
        m_priceOrderMap = std::move(other.m_priceOrderMap);
    }
    return *this;
}

void OrderBookB::add_order(uint64_t id, Side side, double price, long qty) {
    long tick_price = price_to_ticks(price);
    Order* order_ptr = nullptr;
    if (auto it = m_priceOrderMap.find(tick_price); it != m_priceOrderMap.end()) {
        order_ptr = it->second;
        order_ptr->qty += qty;
    } else {
        order_ptr = new Order{tick_price, qty, side};
        m_priceOrderMap[tick_price] = order_ptr;
        if (side == Side::Buy) {
            m_bids.push(order_ptr);
        } else {
            m_asks.push(order_ptr);
        }
    }
    m_orders[id] = std::make_pair(order_ptr, qty);
}

bool OrderBookB::cancel_order(uint64_t id) {
    if (auto it = m_orders.find(id); it != m_orders.end()) {
        Order* order_ptr = it->second.first;
        long orig_qty = it->second.second;
        order_ptr->qty -= orig_qty;
        m_orders.erase(it);
        return true;
    }
    return false;
}

BBO OrderBookB::get_bbo() const {
    BBO bbo;
    while (!m_bids.empty() && m_bids.top()->qty == 0) {
        Order* top = m_bids.top();
        m_bids.pop();
        m_priceOrderMap.erase(top->price);
        delete top;
    }
    if (!m_bids.empty()) {
        bbo.has_bid = true;
        bbo.bid_level = Level{ticks_to_price(m_bids.top()->price), m_bids.top()->qty};
    }

    while (!m_asks.empty() && m_asks.top()->qty == 0) {
        Order* top = m_asks.top();
        m_asks.pop();
        m_priceOrderMap.erase(top->price);
        delete top;
    }
    if (!m_asks.empty()) {
        bbo.has_ask = true;
        bbo.ask_level = Level{ticks_to_price(m_asks.top()->price), m_asks.top()->qty};
    }

    return bbo;
}

std::vector<Level> OrderBookB::get_bids(int depth) const {
    std::vector<Level> result;
    if (depth <= 0) return result;
    std::vector<Order*> active_popped;

    while (!m_bids.empty() && result.size() < static_cast<size_t>(depth)) {
        Order* top = m_bids.top();
        m_bids.pop();
        if (top->qty == 0) {
            m_priceOrderMap.erase(top->price);
            delete top;
        } else {
            result.push_back(Level{ticks_to_price(top->price), top->qty});
            active_popped.push_back(top);
        }
    }

    for (Order* order : active_popped) {
        m_bids.push(order);
    }

    return result;
}

std::vector<Level> OrderBookB::get_asks(int depth) const {
    std::vector<Level> result;
    if (depth <= 0) return result;
    std::vector<Order*> active_popped;

    while (!m_asks.empty() && result.size() < static_cast<size_t>(depth)) {
        Order* top = m_asks.top();
        m_asks.pop();
        if (top->qty == 0) {
            m_priceOrderMap.erase(top->price);
            delete top;
        } else {
            result.push_back(Level{ticks_to_price(top->price), top->qty});
            active_popped.push_back(top);
        }
    }

    for (Order* order : active_popped) {
        m_asks.push(order);
    }

    return result;
}

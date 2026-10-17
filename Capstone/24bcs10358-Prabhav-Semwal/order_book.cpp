#include "order_book.h"

#include <algorithm>
#include <stdexcept>
#include <utility>

bool operator==(const Level& a, const Level& b) {
    return a.price == b.price && a.qty == b.qty;
}

bool operator==(const BBO& a, const BBO& b) {
    return a.has_bid == b.has_bid && a.has_ask == b.has_ask &&
           (!a.has_bid || (a.bid_price == b.bid_price && a.bid_qty == b.bid_qty)) &&
           (!a.has_ask || (a.ask_price == b.ask_price && a.ask_qty == b.ask_qty));
}

void validate(Side side, int price, long qty) {
    bool buy = side == Side::Buy && price >= 5000 && price <= 9999;
    bool sell = side == Side::Sell && price >= 10001 && price <= 15000;
    if ((!buy && !sell) || qty <= 0)
        throw std::invalid_argument("invalid side, price or quantity");
}

void MapBook::add_order(std::uint64_t id, Side side, int price, long qty) {
    validate(side, price, qty);
    if (!orders.emplace(id, Order{side, price, qty}).second)
        throw std::invalid_argument("duplicate live order ID");
    if (side == Side::Buy) bids[price] += qty;
    else asks[price] += qty;
}

bool MapBook::cancel_order(std::uint64_t id) {
    auto it = orders.find(id);
    if (it == orders.end()) return false;
    const auto order = it->second;
    if (order.side == Side::Buy) {
        auto level = bids.find(order.price);
        level->second -= order.qty;
        if (level->second == 0) bids.erase(level);
    } else {
        auto level = asks.find(order.price);
        level->second -= order.qty;
        if (level->second == 0) asks.erase(level);
    }
    orders.erase(it);
    return true;
}

BBO MapBook::get_bbo() const {
    BBO bbo;
    if (!bids.empty()) {
        bbo.has_bid = true;
        bbo.bid_price = bids.begin()->first;
        bbo.bid_qty = bids.begin()->second;
    }
    if (!asks.empty()) {
        bbo.has_ask = true;
        bbo.ask_price = asks.begin()->first;
        bbo.ask_qty = asks.begin()->second;
    }
    return bbo;
}

std::vector<Level> MapBook::get_bids(int depth) const {
    std::vector<Level> result;
    for (auto it = bids.begin(); depth > 0 && it != bids.end(); ++it, --depth)
        result.push_back({it->first, it->second});
    return result;
}

std::vector<Level> MapBook::get_asks(int depth) const {
    std::vector<Level> result;
    for (auto it = asks.begin(); depth > 0 && it != asks.end(); ++it, --depth)
        result.push_back({it->first, it->second});
    return result;
}

void VectorBook::add_order(std::uint64_t id, Side side, int price, long qty) {
    validate(side, price, qty);
    if (!orders.emplace(id, Order{side, price, qty}).second)
        throw std::invalid_argument("duplicate live order ID");
    auto& levels = side == Side::Buy ? bids : asks;
    auto it = std::lower_bound(levels.begin(), levels.end(), price,
        [](const Level& level, int tick) { return level.price < tick; });
    if (it != levels.end() && it->price == price) it->qty += qty;
    else levels.insert(it, {price, qty});
}

bool VectorBook::cancel_order(std::uint64_t id) {
    auto order = orders.find(id);
    if (order == orders.end()) return false;
    auto& levels = order->second.side == Side::Buy ? bids : asks;
    auto it = std::lower_bound(levels.begin(), levels.end(), order->second.price,
        [](const Level& level, int tick) { return level.price < tick; });
    it->qty -= order->second.qty;
    if (it->qty == 0) levels.erase(it);
    orders.erase(order);
    return true;
}

BBO VectorBook::get_bbo() const {
    BBO bbo;
    if (!bids.empty()) {
        bbo.has_bid = true;
        bbo.bid_price = bids.back().price;
        bbo.bid_qty = bids.back().qty;
    }
    if (!asks.empty()) {
        bbo.has_ask = true;
        bbo.ask_price = asks.front().price;
        bbo.ask_qty = asks.front().qty;
    }
    return bbo;
}

std::vector<Level> VectorBook::get_bids(int depth) const {
    std::vector<Level> result;
    for (auto it = bids.rbegin(); depth > 0 && it != bids.rend(); ++it, --depth)
        result.push_back(*it);
    return result;
}

std::vector<Level> VectorBook::get_asks(int depth) const {
    std::vector<Level> result;
    for (auto it = asks.begin(); depth > 0 && it != asks.end(); ++it, --depth)
        result.push_back(*it);
    return result;
}

std::uint64_t FlatOrders::hash(std::uint64_t id) {
    id ^= id >> 30;
    id *= UINT64_C(0xbf58476d1ce4e5b9);
    id ^= id >> 27;
    id *= UINT64_C(0x94d049bb133111eb);
    return id ^ (id >> 31);
}

Order* FlatOrders::find(std::uint64_t id) {
    auto index = hash(id) & (table.size() - 1);
    while (table[index].state != 0) {
        if (table[index].state == 1 && table[index].id == id)
            return &table[index].order;
        index = (index + 1) & (table.size() - 1);
    }
    return nullptr;
}

void FlatOrders::grow() {
    auto old = std::move(table);
    table = std::vector<Entry>(old.size() * 2);
    filled = 0;
    for (const auto& entry : old)
        if (entry.state == 1) insert(entry.id, entry.order);
}

bool FlatOrders::insert(std::uint64_t id, Order order) {
    if (find(id)) return false;
    if ((filled + 1) * 10 >= table.size() * 7) grow();
    auto index = hash(id) & (table.size() - 1);
    while (table[index].state == 1)
        index = (index + 1) & (table.size() - 1);
    if (table[index].state == 0) ++filled;
    table[index] = {id, order, 1};
    return true;
}

bool FlatOrders::erase(std::uint64_t id) {
    auto index = hash(id) & (table.size() - 1);
    while (table[index].state != 0) {
        if (table[index].state == 1 && table[index].id == id) {
            table[index].state = 2;
            return true;
        }
        index = (index + 1) & (table.size() - 1);
    }
    return false;
}

void ArrayBook::change(const Order& order, long delta) {
    if (order.side == Side::Buy) {
        int index = order.price - 5000;
        bids[index] += delta;
        if (delta > 0) best_bid = std::max(best_bid, index);
        else while (best_bid >= 0 && bids[best_bid] == 0) --best_bid;
    } else {
        int index = order.price - 10001;
        asks[index] += delta;
        if (delta > 0) best_ask = std::min(best_ask, index);
        else while (best_ask < 5000 && asks[best_ask] == 0) ++best_ask;
    }
}

void ArrayBook::add_order(std::uint64_t id, Side side, int price, long qty) {
    validate(side, price, qty);
    Order order{side, price, qty};
    if (!orders.insert(id, order))
        throw std::invalid_argument("duplicate live order ID");
    change(order, qty);
}

bool ArrayBook::cancel_order(std::uint64_t id) {
    auto found = orders.find(id);
    if (!found) return false;
    Order order = *found;
    orders.erase(id);
    change(order, -order.qty);
    return true;
}

BBO ArrayBook::get_bbo() const {
    BBO bbo;
    if (best_bid >= 0) {
        bbo.has_bid = true;
        bbo.bid_price = best_bid + 5000;
        bbo.bid_qty = bids[best_bid];
    }
    if (best_ask < 5000) {
        bbo.has_ask = true;
        bbo.ask_price = best_ask + 10001;
        bbo.ask_qty = asks[best_ask];
    }
    return bbo;
}

std::vector<Level> ArrayBook::get_bids(int depth) const {
    std::vector<Level> result;
    for (int i = best_bid; i >= 0 && depth > 0; --i) {
        if (bids[i] == 0) continue;
        result.push_back({i + 5000, bids[i]});
        --depth;
    }
    return result;
}

std::vector<Level> ArrayBook::get_asks(int depth) const {
    std::vector<Level> result;
    for (int i = best_ask; i < 5000 && depth > 0; ++i) {
        if (asks[i] == 0) continue;
        result.push_back({i + 10001, asks[i]});
        --depth;
    }
    return result;
}

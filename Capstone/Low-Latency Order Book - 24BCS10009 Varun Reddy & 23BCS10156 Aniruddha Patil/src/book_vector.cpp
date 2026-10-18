#include "book_vector.hpp"

#include <algorithm>

// ----------------------------- price levels --------------------------------

template <class Better>
std::vector<OrderBookVector::LevelEntry>::iterator OrderBookVector::SortedLevels<Better>::find_pos(int32_t tick) {
    // First entry that is not worse than `tick` (entries are sorted worst -> best).
    return std::lower_bound(v_.begin(), v_.end(), tick,
                            [](const LevelEntry& e, int32_t t) { return Better{}(t, e.tick); });
}

template <class Better>
void OrderBookVector::SortedLevels<Better>::add(int32_t tick, long qty) {
    auto it = find_pos(tick);
    if (it != v_.end() && it->tick == tick)
        it->qty += qty;
    else
        v_.insert(it, LevelEntry{tick, qty});  // shifts every better level one slot right
}

template <class Better>
void OrderBookVector::SortedLevels<Better>::reduce(int32_t tick, long qty) {
    auto it = find_pos(tick);
    it->qty -= qty;
    if (it->qty == 0) v_.erase(it);  // shifts every better level one slot left
}

template <class Better>
std::vector<Level> OrderBookVector::SortedLevels<Better>::top(int depth) const {
    std::vector<Level> out;
    if (depth <= 0) return out;
    const size_t n = std::min(v_.size(), static_cast<size_t>(depth));
    out.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        const LevelEntry& e = v_[v_.size() - 1 - i];
        out.push_back({px::to_price(e.tick), e.qty});
    }
    return out;
}

// ------------------------------- orders ------------------------------------

std::vector<OrderBookVector::Order>::iterator OrderBookVector::find_order(uint64_t id) {
    auto it = std::lower_bound(orders_.begin(), orders_.end(), id,
                               [](const Order& o, uint64_t key) { return o.id < key; });
    return (it != orders_.end() && it->id == id) ? it : orders_.end();
}

void OrderBookVector::compact() {
    orders_.erase(std::remove_if(orders_.begin(), orders_.end(), [](const Order& o) { return o.qty == 0; }),
                  orders_.end());
    dead_ = 0;
}

bool OrderBookVector::add_order(uint64_t id, Side side, double price, long qty) {
    int32_t tick;
    if (!px::validate_order(side, price, qty, tick)) return false;

    const Order order{id, tick, side, qty};
    if (orders_.empty() || orders_.back().id < id) {
        orders_.push_back(order);  // common case: ids arrive in increasing order
    } else {
        auto it = std::lower_bound(orders_.begin(), orders_.end(), id,
                                   [](const Order& o, uint64_t key) { return o.id < key; });
        if (it != orders_.end() && it->id == id) {
            if (it->qty != 0) return false;  // duplicate live id
            *it = order;                     // id of a cancelled order: reuse its tombstone
            --dead_;
        } else {
            orders_.insert(it, order);  // out-of-order id: O(n) shift
        }
    }

    if (side == Side::Buy)
        bids_.add(tick, qty);
    else
        asks_.add(tick, qty);
    return true;
}

bool OrderBookVector::cancel_order(uint64_t id) {
    auto it = find_order(id);
    if (it == orders_.end() || it->qty == 0) return false;

    if (it->side == Side::Buy)
        bids_.reduce(it->tick, it->qty);
    else
        asks_.reduce(it->tick, it->qty);

    it->qty = 0;  // tombstone
    if (++dead_ * 2 > orders_.size()) compact();
    return true;
}

BBO OrderBookVector::get_bbo() const {
    BBO b{};
    if (!bids_.empty()) {
        b.has_bid = true;
        b.bid_price = px::to_price(bids_.best().tick);
        b.bid_qty = bids_.best().qty;
    }
    if (!asks_.empty()) {
        b.has_ask = true;
        b.ask_price = px::to_price(asks_.best().tick);
        b.ask_qty = asks_.best().qty;
    }
    return b;
}

std::vector<Level> OrderBookVector::get_bids(int depth) const { return bids_.top(depth); }
std::vector<Level> OrderBookVector::get_asks(int depth) const { return asks_.top(depth); }

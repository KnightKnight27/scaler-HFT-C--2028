
#include "segment_tree_book.hpp"

#include <algorithm>
#include <iterator>

namespace orderbook {

// ---------- Segment tree ----------

void SegmentTreeOrderBook::SegmentTree::set_active(
    std::size_t index, bool active
) {
    update(1, 0, PRICE_COUNT - 1, index, active);
}

void SegmentTreeOrderBook::SegmentTree::update(
    std::size_t node, std::size_t left, std::size_t right,
    std::size_t index, bool active
) {
    if (left == right) {
        tree_[node] = active ? 1 : 0;
        return;
    }

    const std::size_t mid = left + (right - left) / 2;

    if (index <= mid)
        update(node * 2, left, mid, index, active);
    else
        update(node * 2 + 1, mid + 1, right, index, active);

    tree_[node] = tree_[node * 2] + tree_[node * 2 + 1];
}

int SegmentTreeOrderBook::SegmentTree::first() const {
    return tree_[1] == 0 ? -1 : find_first(1, 0, PRICE_COUNT - 1);
}

int SegmentTreeOrderBook::SegmentTree::last() const {
    return tree_[1] == 0 ? -1 : find_last(1, 0, PRICE_COUNT - 1);
}

int SegmentTreeOrderBook::SegmentTree::find_first(
    std::size_t node, std::size_t left, std::size_t right
) const {
    if (left == right)
        return static_cast<int>(left);

    const std::size_t mid = left + (right - left) / 2;

    if (tree_[node * 2] > 0)
        return find_first(node * 2, left, mid);

    return find_first(node * 2 + 1, mid + 1, right);
}

int SegmentTreeOrderBook::SegmentTree::find_last(
    std::size_t node, std::size_t left, std::size_t right
) const {
    if (left == right)
        return static_cast<int>(left);

    const std::size_t mid = left + (right - left) / 2;

    if (tree_[node * 2 + 1] > 0)
        return find_last(node * 2 + 1, mid + 1, right);

    return find_last(node * 2, left, mid);
}

std::vector<int> SegmentTreeOrderBook::SegmentTree::get_indices(
    std::size_t depth, bool descending
) const {
    std::vector<int> result;

    if (depth == 0 || tree_[1] == 0)
        return result;

    result.reserve(std::min(depth, static_cast<std::size_t>(tree_[1])));
    collect(1, 0, PRICE_COUNT - 1, depth, descending, result);

    return result;
}

void SegmentTreeOrderBook::SegmentTree::collect(
    std::size_t node, std::size_t left, std::size_t right,
    std::size_t depth, bool descending, std::vector<int>& result
) const {
    if (tree_[node] == 0 || result.size() >= depth)
        return;

    if (left == right) {
        result.push_back(static_cast<int>(left));
        return;
    }

    const std::size_t mid = left + (right - left) / 2;

    if (descending) {
        collect(node * 2 + 1, mid + 1, right, depth, descending, result);
        collect(node * 2, left, mid, depth, descending, result);
    } else {
        collect(node * 2, left, mid, depth, descending, result);
        collect(node * 2 + 1, mid + 1, right, depth, descending, result);
    }
}

// ---------- Price helpers ----------

bool SegmentTreeOrderBook::valid_price(Side side, Price price) {
    if (side == Side::Buy)
        return price >= BID_MIN && price <= BID_MAX;

    return price >= ASK_MIN && price <= ASK_MAX;
}

std::size_t SegmentTreeOrderBook::price_to_index(Side side, Price price) {
    return static_cast<std::size_t>(
        side == Side::Buy ? price - BID_MIN : price - ASK_MIN
    );
}

Price SegmentTreeOrderBook::index_to_price(
    Side side, std::size_t index
) {
    return (side == Side::Buy ? BID_MIN : ASK_MIN)
         + static_cast<Price>(index);
}

// ---------- Order book ----------

bool SegmentTreeOrderBook::add_order(
    OrderId id, Side side, Price price, Quantity qty
) {
    if (id == 0 || qty <= 0 || !valid_price(side, price) ||
        used_ids_.count(id) != 0) {
        return false;
    }

    const std::size_t index = price_to_index(side, price);
    auto& levels = side == Side::Buy ? bids_ : asks_;
    auto& tree = side == Side::Buy ? bid_tree_ : ask_tree_;
    PriceLevel& level = levels[index];

    // Record the ID permanently, including after future cancellation.
    used_ids_.insert(id);

    level.orders.push_back(Order{id, qty});
    auto order_it = std::prev(level.orders.end());

    level.total_qty += qty;

    if (level.orders.size() == 1)
        tree.set_active(index, true);

    order_index_.emplace(
        id, OrderLocation{side, index, order_it}
    );

    return true;
}

bool SegmentTreeOrderBook::cancel_order(OrderId id) {
    auto it = order_index_.find(id);

    if (it == order_index_.end())
        return false;

    const OrderLocation location = it->second;

    auto& levels = location.side == Side::Buy ? bids_ : asks_;
    auto& tree = location.side == Side::Buy ? bid_tree_ : ask_tree_;
    PriceLevel& level = levels[location.index];

    level.total_qty -= location.iterator->qty;
    level.orders.erase(location.iterator);

    if (level.orders.empty()) {
        level.total_qty = 0;
        tree.set_active(location.index, false);
    }

    order_index_.erase(it);

    // Intentionally retain the ID in used_ids_.
    return true;
}

BBO SegmentTreeOrderBook::get_bbo() const {
    BBO result;

    const int bid = bid_tree_.last();

    if (bid >= 0) {
        const auto index = static_cast<std::size_t>(bid);
        result.has_bid = true;
        result.bid_price = index_to_price(Side::Buy, index);
        result.bid_qty = bids_[index].total_qty;
    }

    const int ask = ask_tree_.first();

    if (ask >= 0) {
        const auto index = static_cast<std::size_t>(ask);
        result.has_ask = true;
        result.ask_price = index_to_price(Side::Sell, index);
        result.ask_qty = asks_[index].total_qty;
    }

    return result;
}

std::vector<Level> SegmentTreeOrderBook::get_bids(
    std::size_t depth
) const {
    const auto indices = bid_tree_.get_indices(depth, true);

    std::vector<Level> result;
    result.reserve(indices.size());

    for (int raw_index : indices) {
        const auto index = static_cast<std::size_t>(raw_index);
        result.push_back(Level{
            index_to_price(Side::Buy, index),
            bids_[index].total_qty
        });
    }

    return result;
}

std::vector<Level> SegmentTreeOrderBook::get_asks(
    std::size_t depth
) const {
    const auto indices = ask_tree_.get_indices(depth, false);

    std::vector<Level> result;
    result.reserve(indices.size());

    for (int raw_index : indices) {
        const auto index = static_cast<std::size_t>(raw_index);
        result.push_back(Level{
            index_to_price(Side::Sell, index),
            asks_[index].total_qty
        });
    }

    return result;
}

} // namespace orderbook

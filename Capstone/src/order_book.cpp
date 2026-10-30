#include "order_book/order_book.h"

#include <algorithm>

namespace {

template <typename BookSideType> std::optional<LevelSnapshot> bestOf(const BookSideType &side) {
  const PriceLevel *level = side.best();
  return level ? std::optional{level->snapshot()} : std::nullopt;
}

} // namespace

OrderBook::OrderBook(std::size_t expected_orders) : pool_(expected_orders) {
  order_locations_.reserve(expected_orders);
}

std::expected<std::span<const Trade>, BookError> OrderBook::addOrder(Order order) {
  if (order.qty == 0) {
    return std::unexpected(BookError::ZeroQuantity);
  }
  if (order.price <= 0) {
    return std::unexpected(BookError::NonPositivePrice);
  }
  if (order_locations_.contains(order.id)) {
    return std::unexpected(BookError::DuplicateOrderId);
  }

  trades_.clear();
  if (order.side == Side::Buy) {
    execute(order, bids_, asks_);
  } else {
    execute(order, asks_, bids_);
  }
  return std::span<const Trade>(trades_);
}

bool OrderBook::removeOrder(OrderId id) {
  const auto location = order_locations_.find(id);
  if (location == order_locations_.end()) {
    return false;
  }
  const NodeIndex index = location->second;
  if (pool_[index].order.side == Side::Buy) {
    bids_.remove(index, pool_);
  } else {
    asks_.remove(index, pool_);
  }
  pool_.release(index);
  order_locations_.erase(location);
  return true;
}

std::vector<LevelSnapshot> OrderBook::getBids(std::size_t depth) const {
  return bids_.snapshot(depth);
}

std::vector<LevelSnapshot> OrderBook::getAsks(std::size_t depth) const {
  return asks_.snapshot(depth);
}

BBO OrderBook::getBBO() const { return {bestOf(bids_), bestOf(asks_)}; }

template <typename OwnSide, typename OppositeSide>
void OrderBook::execute(Order &order, OwnSide &own, OppositeSide &opposite) {
  match(order, opposite);
  if (order.qty == 0) {
    return;
  }
  const NodeIndex index = pool_.acquire(order);
  order_locations_.emplace(order.id, index);
  own.append(index, pool_);
}

template <typename RestingSide> void OrderBook::match(Order &taker, RestingSide &resting) {
  while (taker.qty > 0) {
    PriceLevel *level = resting.best();
    if (level == nullptr || !RestingSide::crosses(level->price, taker.price)) {
      return;
    }
    while (taker.qty > 0 && !level->empty()) {
      const NodeIndex maker_index = level->head;
      Order &maker = pool_[maker_index].order;
      const Quantity fill_qty = std::min(taker.qty, maker.qty);

      trades_.push_back({maker.id, taker.id, level->price, fill_qty, taker.timestamp_ns});
      taker.qty -= fill_qty;
      maker.qty -= fill_qty;
      level->total_qty -= fill_qty;

      if (maker.qty == 0) {
        level->detach(maker_index, pool_);
        order_locations_.erase(maker.id);
        pool_.release(maker_index);
      }
    }
    resting.eraseBestIfEmpty();
  }
}

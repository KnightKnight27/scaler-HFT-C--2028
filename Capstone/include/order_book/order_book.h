#pragma once

#include <cstddef>
#include <expected>
#include <limits>
#include <span>
#include <unordered_map>
#include <vector>

#include "order_book/book_side.h"

class OrderBook {
public:
  static constexpr std::size_t kDefaultCapacity = 1 << 16;
  static constexpr std::size_t kFullDepth = std::numeric_limits<std::size_t>::max();

  explicit OrderBook(std::size_t expected_orders = kDefaultCapacity);

  [[nodiscard]] std::expected<std::span<const Trade>, BookError> addOrder(Order order);
  [[nodiscard]] bool removeOrder(OrderId id);

  [[nodiscard]] std::vector<LevelSnapshot> getBids(std::size_t depth = kFullDepth) const;
  [[nodiscard]] std::vector<LevelSnapshot> getAsks(std::size_t depth = kFullDepth) const;
  [[nodiscard]] BBO getBBO() const;
  [[nodiscard]] std::size_t restingOrderCount() const { return order_locations_.size(); }

private:
  OrderPool pool_;
  BidSide bids_;
  AskSide asks_;
  std::unordered_map<OrderId, NodeIndex> order_locations_;
  std::vector<Trade> trades_;

  template <typename OwnSide, typename OppositeSide>
  void execute(Order &order, OwnSide &own, OppositeSide &opposite);

  template <typename RestingSide> void match(Order &taker, RestingSide &resting);
};

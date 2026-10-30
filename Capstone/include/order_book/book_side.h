#pragma once

#include <algorithm>
#include <cstddef>
#include <vector>

#include "order_book/price_level.h"

template <Side RestingSide> class BookSide {
public:
  [[nodiscard]] static constexpr bool crosses(Price resting, Price incoming) {
    return !isWorse(resting, incoming);
  }

  [[nodiscard]] PriceLevel *best() { return levels_.empty() ? nullptr : &levels_.back(); }

  [[nodiscard]] const PriceLevel *best() const {
    return levels_.empty() ? nullptr : &levels_.back();
  }

  void eraseBestIfEmpty() {
    if (!levels_.empty() && levels_.back().empty()) {
      levels_.pop_back();
    }
  }

  void append(NodeIndex index, OrderPool &pool) {
    levelAt(pool[index].order.price).pushBack(index, pool);
  }

  void remove(NodeIndex index, OrderPool &pool) {
    const auto level = lowerBound(pool[index].order.price);
    level->detach(index, pool);
    if (level->empty()) {
      levels_.erase(level);
    }
  }

  [[nodiscard]] std::vector<LevelSnapshot> snapshot(std::size_t depth) const {
    const auto level_count = std::min(depth, levels_.size());
    std::vector<LevelSnapshot> result;
    result.reserve(level_count);
    std::ranges::transform(levels_.rbegin(), levels_.rbegin() + level_count,
                           std::back_inserter(result), &PriceLevel::snapshot);
    return result;
  }

private:
  std::vector<PriceLevel> levels_;

  static constexpr bool isWorse(Price lhs, Price rhs) {
    if constexpr (RestingSide == Side::Buy) {
      return lhs < rhs;
    } else {
      return lhs > rhs;
    }
  }

  auto lowerBound(Price price) {
    return std::ranges::lower_bound(levels_, price, isWorse, &PriceLevel::price);
  }

  PriceLevel &levelAt(Price price) {
    auto level = lowerBound(price);
    if (level == levels_.end() || level->price != price) {
      level = levels_.insert(level, PriceLevel{price});
    }
    return *level;
  }
};

using BidSide = BookSide<Side::Buy>;
using AskSide = BookSide<Side::Sell>;

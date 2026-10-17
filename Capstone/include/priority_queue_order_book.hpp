#ifndef PRIORITY_QUEUE_ORDER_BOOK_HPP
#define PRIORITY_QUEUE_ORDER_BOOK_HPP

#include "types.hpp"
#include <cmath>
#include <queue>
#include <unordered_map>
#include <vector>
struct PriorityOrder {
  std::uint64_t id;
  std::uint64_t price;
  long qty;
  Side side;
};

struct BidComparator {
  bool operator()(const PriorityOrder &a, const PriorityOrder &b) const {
    if (a.price < b.price) {
      return true;
    } else if (a.price == b.price) {
      return a.id > b.id;
    }
    return false;
  }
};

struct AskComparator {
  bool operator()(const PriorityOrder &a, const PriorityOrder &b) const {
    if (a.price > b.price) {
      return true;
    } else if (a.price == b.price) {
      return a.id > b.id;
    }
    return false;
  }
};

class PriorityQueueOrderBook {
private:
  mutable std::priority_queue<PriorityOrder, std::vector<PriorityOrder>,
                              BidComparator>
      bids;
  mutable std::priority_queue<PriorityOrder, std::vector<PriorityOrder>,
                              AskComparator>
      asks;
  std::vector<bool> isCancelled;
  std::vector<PriorityOrder> orderLookup;
  std::unordered_map<std::uint64_t, long> bid_map;
  std::unordered_map<std::uint64_t, long> ask_map;

  void cleanTopBids() const {
    while (!bids.empty()) {
      std::uint64_t topId = bids.top().id;
      if (topId < isCancelled.size() && isCancelled[topId]) {
        bids.pop();
      } else {
        break;
      }
    }
  }

  void cleanTopAsks() const {
    while (!asks.empty()) {
      std::uint64_t topId = asks.top().id;
      if (topId < isCancelled.size() && isCancelled[topId]) {
        asks.pop();
      } else {
        break;
      }
    }
  }

public:
  PriorityQueueOrderBook()
      : isCancelled(MAX_ORDERS, false),
        orderLookup(MAX_ORDERS, PriorityOrder{0, 0, 0, Side::Buy}) {}

  void add_order(std::uint64_t id, Side side, double priceDouble, long qty) {
    if (id >= MAX_ORDERS) {
      return;
    }
    std::uint64_t price = std::llround(priceDouble * 100.0);

    PriorityOrder order{id, price, qty, side};
    orderLookup[id] = order;
    isCancelled[id] = false;

    if (side == Side::Buy) {
      bid_map[price] += qty;
      bids.push(order);
    } else {
      ask_map[price] += qty;
      asks.push(order);
    }
  }

  bool cancel_order(std::uint64_t id) {
    if (id >= isCancelled.size() || isCancelled[id] ||
        orderLookup[id].qty == 0) {
      return false;
    }

    isCancelled[id] = true;
    const PriorityOrder &order = orderLookup[id];

    if (order.side == Side::Buy) {
      auto it = bid_map.find(order.price);
      if (it != bid_map.end()) {
        if (it->second <= order.qty) {
          bid_map.erase(it);
        } else {
          it->second -= order.qty;
        }
      }
    } else {
      auto it = ask_map.find(order.price);
      if (it != ask_map.end()) {
        if (it->second <= order.qty) {
          ask_map.erase(it);
        } else {
          it->second -= order.qty;
        }
      }
    }

    cleanTopBids();
    cleanTopAsks();
    return true;
  }

  BBO get_bbo() const {
    cleanTopAsks();
    cleanTopBids();

    BBO bbo{};

    if (!bids.empty()) {
      std::uint64_t p = bids.top().price;
      auto it = bid_map.find(p);
      long qty = (it != bid_map.end()) ? it->second : 0;
      bbo.has_bid = true;
      bbo.bid_price = p / 100.0;
      bbo.bid_qty = qty;
    }
    if (!asks.empty()) {
      std::uint64_t p = asks.top().price;
      auto it = ask_map.find(p);
      long qty = (it != ask_map.end()) ? it->second : 0;
      bbo.has_ask = true;
      bbo.ask_price = p / 100.0;
      bbo.ask_qty = qty;
    }

    return bbo;
  }

  std::vector<Level> get_bids(int depth = 10) const {
    cleanTopBids();
    std::vector<Level> result;
    result.reserve(depth);
    if (bids.empty()) {
      return result;
    }

    auto temp = bids;
    std::uint64_t current_price = 0;
    long current_qty = 0;
    const std::size_t max_depth = (depth > 0) ? depth : 0;

    while (!temp.empty() && result.size() < max_depth) {
      PriorityOrder top = temp.top();
      temp.pop();
      if (top.id < isCancelled.size() && isCancelled[top.id]) {
        continue;
      }
      if (current_price == 0) {
        current_price = top.price;
        current_qty = top.qty;
      } else if (top.price == current_price) {
        current_qty += top.qty;
      } else {
        result.push_back({current_price / 100.0, current_qty});
        current_price = top.price;
        current_qty = top.qty;
      }
    }
    if (current_price != 0 && result.size() < max_depth) {
      result.push_back({current_price / 100.0, current_qty});
    }
    return result;
  }

  std::vector<Level> get_asks(int depth = 10) const {
    cleanTopAsks();
    std::vector<Level> result;
    result.reserve(depth);
    if (asks.empty()) {
      return result;
    }

    auto temp = asks;
    std::uint64_t current_price = 0;
    long current_qty = 0;
    const std::size_t max_depth = (depth > 0) ? depth : 0;

    while (!temp.empty() && result.size() < max_depth) {
      PriorityOrder top = temp.top();
      temp.pop();
      if (top.id < isCancelled.size() && isCancelled[top.id]) {
        continue;
      }
      if (current_price == 0) {
        current_price = top.price;
        current_qty = top.qty;
      } else if (top.price == current_price) {
        current_qty += top.qty;
      } else {
        result.push_back({current_price / 100.0, current_qty});
        current_price = top.price;
        current_qty = top.qty;
      }
    }
    if (current_price != 0 && result.size() < max_depth) {
      result.push_back({current_price / 100.0, current_qty});
    }
    return result;
  }
};

#endif

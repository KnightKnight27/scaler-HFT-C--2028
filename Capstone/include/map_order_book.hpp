#ifndef MAP_ORDER_BOOK_HPP
#define MAP_ORDER_BOOK_HPP

#include "order_node.hpp"
#include "order_pool.hpp"
#include "price_level.hpp"
#include "types.hpp"
#include <cmath>
#include <functional>
#include <map>
#include <vector>

class MapOrderBook {
private:
  std::map<std::uint64_t, PriceLevel, std::greater<std::uint64_t>> bids;
  std::map<std::uint64_t, PriceLevel, std::less<std::uint64_t>> asks;
  std::vector<OrderNode *> orderLookupTable;
  OrderPool pool;

public:
  MapOrderBook() : pool(MAX_ORDERS), orderLookupTable(MAX_ORDERS, nullptr) {}
  void add_order(std::uint64_t id, Side side, double priceDouble, long qty) {
    std::uint64_t price = std::llround(priceDouble * 100.0);
    if (id >= MAX_ORDERS) {
      return;
    }

    OrderNode *node = pool.allocate();

    node->id = id;
    node->price = price;
    node->qty = qty;
    node->side = side;
    node->next = nullptr;
    node->prev = nullptr;

    if (side == Side::Buy) {
      bids[price].addOrder(node);
    } else {
      asks[price].addOrder(node);
    }

    orderLookupTable[id] = node;
  }

  bool cancel_order(std::uint64_t id) {
    if (id >= MAX_ORDERS) {
      return false;
    }

    OrderNode *node = orderLookupTable[id];

    if (node == nullptr) {
      return false;
    }

    std::uint64_t price = node->price;
    bool cancelled = false;
    if (node->side == Side::Buy) {
      auto it = bids.find(price);

      if (it != bids.end()) {
        it->second.removeOrder(node);

        if (it->second.orderCount == 0) {
          bids.erase(it);
        }
        cancelled = true;
      }
    } else {
      auto it = asks.find(price);

      if (it != asks.end()) {
        it->second.removeOrder(node);

        if (it->second.orderCount == 0) {
          asks.erase(it);
        }
        cancelled = true;
      }
    }

    orderLookupTable[id] = nullptr;
    pool.deallocate(node);
    return cancelled;
  }

  BBO get_bbo() const {
    BBO bbo{};

    if (!bids.empty()) {
      auto it = bids.begin();
      bbo.has_bid = true;
      bbo.bid_price = it->first / 100.0;
      bbo.bid_qty = it->second.totalQuantity;
    }

    if (!asks.empty()) {
      auto it = asks.begin();
      bbo.has_ask = true;
      bbo.ask_price = it->first / 100.0;
      bbo.ask_qty = it->second.totalQuantity;
    }

    return bbo;
  }

  std::vector<Level> get_bids(int depth = 10) const {
    std::vector<Level> result;
    result.reserve(depth);
    const std::size_t max_depth = (depth > 0) ? depth : 0;
    for (auto it = bids.begin();
         it != bids.end() && result.size() < max_depth; ++it) {
      if (it->second.orderCount > 0) {
        result.push_back({it->first / 100.0, it->second.totalQuantity});
      }
    }
    return result;
  }

  std::vector<Level> get_asks(int depth = 10) const {
    std::vector<Level> result;
    result.reserve(depth);
    const std::size_t max_depth = (depth > 0) ? depth : 0;
    for (auto it = asks.begin();
         it != asks.end() && result.size() < max_depth; ++it) {
      if (it->second.orderCount > 0) {
        result.push_back({it->first / 100.0, it->second.totalQuantity});
      }
    }
    return result;
  }
};

#endif
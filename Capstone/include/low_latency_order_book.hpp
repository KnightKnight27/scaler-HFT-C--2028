#ifndef LOW_LATENCY_ORDER_BOOK_HPP
#define LOW_LATENCY_ORDER_BOOK_HPP

#include "order_node.hpp"
#include "order_pool.hpp"
#include "price_level.hpp"
#include "types.hpp"
#include <array>
#include <cmath>
#include <cstddef>
#include <vector>

constexpr std::size_t BASE_PRICE = 5000;
constexpr std::size_t MAX_LEVELS = 10001;

class LowLatencyOrderBook {
private:
  std::array<PriceLevel, MAX_LEVELS> bids{};
  std::array<PriceLevel, MAX_LEVELS> asks{};
  std::vector<OrderNode *> orderLookupTable;
  OrderPool pool;
  std::uint64_t bestBidPrice{0};
  std::uint64_t bestAskPrice{BASE_PRICE + MAX_LEVELS};

public:
  LowLatencyOrderBook()
      : pool(MAX_ORDERS), orderLookupTable(MAX_ORDERS, nullptr) {}

  void add_order(std::uint64_t id, Side side, double priceDouble, long qty) {
    std::uint64_t price = std::llround(priceDouble * 100.0);
    if (id >= MAX_ORDERS || price < BASE_PRICE ||
        price >= BASE_PRICE + MAX_LEVELS) {
      return;
    }

    OrderNode *node = pool.allocate();
    node->id = id;
    node->price = price;
    node->qty = qty;
    node->side = side;

    orderLookupTable[id] = node;

    std::size_t offset = price - BASE_PRICE;

    if (side == Side::Buy) {
      bids[offset].addOrder(node);
      if (price > bestBidPrice) {
        bestBidPrice = price;
      }
    } else {
      asks[offset].addOrder(node);
      if (price < bestAskPrice) {
        bestAskPrice = price;
      }
    }
  }

  bool cancel_order(std::uint64_t id) {
    if (id >= MAX_ORDERS) {
      return false;
    }

    OrderNode *node = orderLookupTable[id];
    if (node == nullptr) {
      return false;
    }

    std::size_t offset = node->price - BASE_PRICE;
    if (node->side == Side::Buy) {
      bids[offset].removeOrder(node);
      if (bids[offset].orderCount == 0 && node->price == bestBidPrice) {
        while (bestBidPrice > BASE_PRICE &&
               bids[bestBidPrice - BASE_PRICE].orderCount == 0) {
          bestBidPrice--;
        }
        if (bids[bestBidPrice - BASE_PRICE].orderCount == 0) {
          bestBidPrice = 0;
        }
      }
    } else {
      asks[offset].removeOrder(node);
      if (asks[offset].orderCount == 0 && node->price == bestAskPrice) {
        while (bestAskPrice < BASE_PRICE + MAX_LEVELS - 1 &&
               asks[bestAskPrice - BASE_PRICE].orderCount == 0) {
          bestAskPrice++;
        }
        if (asks[bestAskPrice - BASE_PRICE].orderCount == 0) {
          bestAskPrice = BASE_PRICE + MAX_LEVELS;
        }
      }
    }

    orderLookupTable[id] = nullptr;
    pool.deallocate(node);
    return true;
  }

  BBO get_bbo() const {
    BBO bbo{};
    if (bestBidPrice >= BASE_PRICE &&
        bids[bestBidPrice - BASE_PRICE].orderCount > 0) {
      bbo.has_bid = true;
      bbo.bid_price = bestBidPrice / 100.0;
      bbo.bid_qty = bids[bestBidPrice - BASE_PRICE].totalQuantity;
    }
    if (bestAskPrice < BASE_PRICE + MAX_LEVELS &&
        asks[bestAskPrice - BASE_PRICE].orderCount > 0) {
      bbo.has_ask = true;
      bbo.ask_price = bestAskPrice / 100.0;
      bbo.ask_qty = asks[bestAskPrice - BASE_PRICE].totalQuantity;
    }
    return bbo;
  }

  std::vector<Level> get_bids(int depth = 10) const {
    std::vector<Level> result;
    result.reserve(depth);
    if (bestBidPrice < BASE_PRICE ||
        bids[bestBidPrice - BASE_PRICE].orderCount == 0) {
      return result;
    }

    const std::size_t max_depth = (depth > 0) ? depth : 0;
    for (std::size_t p = bestBidPrice;
         p >= BASE_PRICE && result.size() < max_depth; p--) {
      std::size_t offset = p - BASE_PRICE;
      if (bids[offset].orderCount > 0) {
        result.push_back({p / 100.0, bids[offset].totalQuantity});
      }
    }
    return result;
  }

  std::vector<Level> get_asks(int depth = 10) const {
    std::vector<Level> result;
    result.reserve(depth);
    if (bestAskPrice >= BASE_PRICE + MAX_LEVELS ||
        asks[bestAskPrice - BASE_PRICE].orderCount == 0) {
      return result;
    }

    const std::size_t max_depth = (depth > 0) ? depth : 0;
    for (std::size_t p = bestAskPrice;
         p < BASE_PRICE + MAX_LEVELS && result.size() < max_depth; p++) {
      std::size_t offset = p - BASE_PRICE;
      if (asks[offset].orderCount > 0) {
        result.push_back({p / 100.0, asks[offset].totalQuantity});
      }
    }
    return result;
  }
};

#endif
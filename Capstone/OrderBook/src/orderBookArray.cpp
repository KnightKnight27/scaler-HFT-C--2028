#include "orderBookArray.hpp"
#include <algorithm>

using namespace std;

OrderBookArray::OrderBookArray()
    : levelQty(MarketConfig::totalLevels, 0), bestBidTick(-1), bestAskTick(-1) {
  slots.reserve(1 << 20);
}

void OrderBookArray::addOrder(uint64_t id, Side side, double price, long qty) {
  int tick = priceToTick(price);
  if (!validTick(tick))
    return;

  Order order{id, side, tick, qty};

  if (id < maxDirectId) {
    if (id >= slots.size()) {
      size_t newSize = max(slots.size() * 2, (size_t)id + 1);
      newSize = min(newSize, (size_t)maxDirectId);
      slots.resize(newSize);
    }
    slots[id].order = order;
    slots[id].live = true;
  } else {
    bigIdOrders[id] = order;
  }

  levelQty[tick] += qty;

  if (side == Side::Buy) {
    if (bestBidTick == -1 || tick > bestBidTick)
      bestBidTick = tick;
  } else {
    if (bestAskTick == -1 || tick < bestAskTick)
      bestAskTick = tick;
  }
}

bool OrderBookArray::cancelOrder(uint64_t id) {
  Order order;

  if (id < slots.size() && slots[id].live) {
    order = slots[id].order;
    slots[id].live = false;
  } else {
    auto it = bigIdOrders.find(id);
    if (it == bigIdOrders.end())
      return false;
    order = it->second;
    bigIdOrders.erase(it);
  }

  int tick = order.tick;
  long qty = order.qty;

  long r = levelQty[tick] - qty;
  if (r > 0) {
    levelQty[tick] = r;
  } else {
    levelQty[tick] = 0;

    // scan for next active price if best price level was emptied
    if (order.side == Side::Buy && tick == bestBidTick) {
      while (bestBidTick >= 0 && levelQty[bestBidTick] == 0) {
        bestBidTick--;
      }
    } else if (order.side == Side::Sell && tick == bestAskTick) {
      int totalLevels = MarketConfig::totalLevels;
      while (bestAskTick < totalLevels && levelQty[bestAskTick] == 0) {
        bestAskTick++;
      }
      if (bestAskTick >= totalLevels)
        bestAskTick = -1;
    }
  }

  return true;
}

BBO OrderBookArray::getBBO() const {
  BBO bbo{false, 0.0, 0, false, 0.0, 0};

  if (bestBidTick != -1 && levelQty[bestBidTick] > 0) {
    bbo.hasBid = true;
    bbo.bidPrice = tickToPrice(bestBidTick);
    bbo.bidQty = levelQty[bestBidTick];
  }

  if (bestAskTick != -1 && levelQty[bestAskTick] > 0) {
    bbo.hasAsk = true;
    bbo.askPrice = tickToPrice(bestAskTick);
    bbo.askQty = levelQty[bestAskTick];
  }

  return bbo;
}

vector<Level> OrderBookArray::getBids(int depth) const {
  vector<Level> levels;
  levels.reserve(depth);

  if (bestBidTick == -1)
    return levels;

  int cnt = 0;
  for (int t = bestBidTick; t >= 0; t--) {
    if (cnt >= depth)
      break;

    long qty = levelQty[t];
    if (qty > 0) {
      double price = tickToPrice(t);
      levels.push_back({price, qty});
      cnt++;
    }
  }

  return levels;
}

vector<Level> OrderBookArray::getAsks(int depth) const {
  vector<Level> levels;
  levels.reserve(depth);

  if (bestAskTick == -1)
    return levels;

  int cnt = 0;
  int totalLevels = MarketConfig::totalLevels;
  for (int t = bestAskTick; t < totalLevels; t++) {
    if (cnt >= depth)
      break;

    long qty = levelQty[t];
    if (qty > 0) {
      double price = tickToPrice(t);
      levels.push_back({price, qty});
      cnt++;
    }
  }

  return levels;
}

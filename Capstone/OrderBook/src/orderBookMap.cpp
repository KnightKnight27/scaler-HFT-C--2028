#include "orderBookMap.hpp"

using namespace std;

void OrderBookMap::addOrder(uint64_t id, Side side, double price, long qty) {
  int tick = priceToTick(price);
  if (!validTick(tick))
    return;

  Order order{id, side, tick, qty};
  orders[id] = order;

  if (side == Side::Buy) {
    bids[tick] += qty;
  } else {
    asks[tick] += qty;
  }
}

bool OrderBookMap::cancelOrder(uint64_t id) {
  auto it = orders.find(id);
  if (it == orders.end())
    return false;

  Order order = it->second;
  int tick = order.tick;
  long qty = order.qty;

  if (order.side == Side::Buy) {
    auto bit = bids.find(tick);
    if (bit != bids.end()) {
      long r = bit->second - qty;
      if (r > 0)
        bit->second = r;
      else
        bids.erase(bit);
    }
  } else {
    auto ait = asks.find(tick);
    if (ait != asks.end()) {
      long r = ait->second - qty;
      if (r > 0)
        ait->second = r;
      else
        asks.erase(ait);
    }
  }

  orders.erase(it);
  return true;
}

BBO OrderBookMap::getBBO() const {
  BBO bbo{false, 0.0, 0, false, 0.0, 0};

  if (!bids.empty()) {
    auto it = bids.begin();
    bbo.hasBid = true;
    bbo.bidPrice = tickToPrice(it->first);
    bbo.bidQty = it->second;
  }

  if (!asks.empty()) {
    auto it = asks.begin();
    bbo.hasAsk = true;
    bbo.askPrice = tickToPrice(it->first);
    bbo.askQty = it->second;
  }

  return bbo;
}

vector<Level> OrderBookMap::getBids(int depth) const {
  vector<Level> levels;
  levels.reserve(depth);

  int cnt = 0;
  for (auto it = bids.begin(); it != bids.end(); ++it) {
    if (cnt >= depth)
      break;

    double price = tickToPrice(it->first);
    long qty = it->second;
    levels.push_back({price, qty});
    cnt++;
  }

  return levels;
}

vector<Level> OrderBookMap::getAsks(int depth) const {
  vector<Level> levels;
  levels.reserve(depth);

  int cnt = 0;
  for (auto it = asks.begin(); it != asks.end(); ++it) {
    if (cnt >= depth)
      break;

    double price = tickToPrice(it->first);
    long qty = it->second;
    levels.push_back({price, qty});
    cnt++;
  }

  return levels;
}

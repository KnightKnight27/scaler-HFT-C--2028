#include "orderBookHeap.hpp"

using namespace std;

void OrderBookHeap::pruneStaleBids() const {
  while (!bidHeap.empty()) {
    int topTick = bidHeap.top();
    auto it = bidLvl.find(topTick);
    bool isLevelActive = (it != bidLvl.end()) && (it->second.qty > 0);
    if (isLevelActive)
      break;

    // level is empty now, drop it from both
    if (it != bidLvl.end())
      bidLvl.erase(it);
    bidHeap.pop();
  }
}

void OrderBookHeap::pruneStaleAsks() const {
  while (!askHeap.empty()) {
    int topTick = askHeap.top();
    auto it = askLvl.find(topTick);
    bool isLevelActive = (it != askLvl.end()) && (it->second.qty > 0);
    if (isLevelActive)
      break;

    if (it != askLvl.end())
      askLvl.erase(it);
    askHeap.pop();
  }
}

void OrderBookHeap::addOrder(uint64_t id, Side side, double price, long qty) {
  int tick = priceToTick(price);
  if (!validTick(tick))
    return;

  Order order{id, side, tick, qty};
  orders[id] = order;

  if (side == Side::Buy) {
    LevelState &lvl = bidLvl[tick];
    if (!lvl.inHeap) {
      bidHeap.push(tick);
      lvl.inHeap = true;
    }
    lvl.qty += qty;
  } else {
    LevelState &lvl = askLvl[tick];
    if (!lvl.inHeap) {
      askHeap.push(tick);
      lvl.inHeap = true;
    }
    lvl.qty += qty;
  }
}

bool OrderBookHeap::cancelOrder(uint64_t id) {
  auto it = orders.find(id);
  if (it == orders.end())
    return false;

  Order order = it->second;
  int tick = order.tick;
  long qty = order.qty;

  // only the qty changes here, the heap is cleaned later in getBBO
  if (order.side == Side::Buy) {
    auto lit = bidLvl.find(tick);
    if (lit != bidLvl.end())
      lit->second.qty -= qty;
  } else {
    auto lit = askLvl.find(tick);
    if (lit != askLvl.end())
      lit->second.qty -= qty;
  }

  orders.erase(it);
  return true;
}

BBO OrderBookHeap::getBBO() const {
  BBO bbo{false, 0.0, 0, false, 0.0, 0};

  pruneStaleBids();
  if (!bidHeap.empty()) {
    int tick = bidHeap.top();
    auto it = bidLvl.find(tick);
    bool isBidLive = (it != bidLvl.end()) && (it->second.qty > 0);
    if (isBidLive) {
      bbo.hasBid = true;
      bbo.bidPrice = tickToPrice(tick);
      bbo.bidQty = it->second.qty;
    }
  }

  pruneStaleAsks();
  if (!askHeap.empty()) {
    int tick = askHeap.top();
    auto it = askLvl.find(tick);
    bool isAskLive = (it != askLvl.end()) && (it->second.qty > 0);
    if (isAskLive) {
      bbo.hasAsk = true;
      bbo.askPrice = tickToPrice(tick);
      bbo.askQty = it->second.qty;
    }
  }

  return bbo;
}

// copy the heap and pop from the copy, so the real one is not touched
vector<Level> OrderBookHeap::getBids(int depth) const {
  vector<Level> levels;
  levels.reserve(depth);

  auto heap = bidHeap;
  int cnt = 0;
  while (!heap.empty() && cnt < depth) {
    int tick = heap.top();
    heap.pop();

    auto it = bidLvl.find(tick);
    if (it != bidLvl.end() && it->second.qty > 0) {
      double price = tickToPrice(tick);
      long qty = it->second.qty;
      levels.push_back({price, qty});
      cnt++;
    }
  }

  return levels;
}

vector<Level> OrderBookHeap::getAsks(int depth) const {
  vector<Level> levels;
  levels.reserve(depth);

  auto heap = askHeap;
  int cnt = 0;
  while (!heap.empty() && cnt < depth) {
    int tick = heap.top();
    heap.pop();

    auto it = askLvl.find(tick);
    if (it != askLvl.end() && it->second.qty > 0) {
      double price = tickToPrice(tick);
      long qty = it->second.qty;
      levels.push_back({price, qty});
      cnt++;
    }
  }

  return levels;
}

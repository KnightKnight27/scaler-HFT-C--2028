#pragma once

#include "order_book/order_pool.h"

struct PriceLevel {
  Price price;
  Quantity total_qty = 0;
  std::uint32_t order_count = 0;
  NodeIndex head = kNullNode;
  NodeIndex tail = kNullNode;

  [[nodiscard]] bool empty() const { return head == kNullNode; }

  [[nodiscard]] LevelSnapshot snapshot() const { return {price, total_qty, order_count}; }

  void pushBack(NodeIndex index, OrderPool &pool) {
    OrderNode &node = pool[index];
    node.prev = tail;
    node.next = kNullNode;
    if (tail == kNullNode) {
      head = index;
    } else {
      pool[tail].next = index;
    }
    tail = index;
    total_qty += node.order.qty;
    ++order_count;
  }

  void detach(NodeIndex index, OrderPool &pool) {
    const OrderNode &node = pool[index];
    if (node.prev == kNullNode) {
      head = node.next;
    } else {
      pool[node.prev].next = node.next;
    }
    if (node.next == kNullNode) {
      tail = node.prev;
    } else {
      pool[node.next].prev = node.prev;
    }
    total_qty -= node.order.qty;
    --order_count;
  }
};

#ifndef ORDER_POOL_HPP
#define ORDER_POOL_HPP

#include "order_node.hpp"
#include <cstddef>
#include <vector>

class OrderPool {
private:
  std::vector<OrderNode> poolStorage;
  OrderNode *freeListHead{nullptr};

public:
  OrderPool(std::size_t capacity) {
    poolStorage.resize(capacity);

    for (std::size_t i = 0; i < capacity - 1; i++) {
      poolStorage[i].next = &poolStorage[i + 1];
    }
    poolStorage[capacity - 1].next = nullptr;
    freeListHead = &poolStorage[0];
  }

  OrderNode *allocate() {
    if (freeListHead == nullptr) {
      return nullptr;
    }

    OrderNode *node = freeListHead;
    freeListHead = freeListHead->next;

    node->id = 0;
    node->price = 0;
    node->qty = 0;
    node->side = Side::Buy;
    node->next = nullptr;
    node->prev = nullptr;

    return node;
  }

  void deallocate(OrderNode *node) {
    if (node == nullptr) {
      return;
    }

    node->next = freeListHead;
    node->prev = nullptr;
    freeListHead = node;
  }
};

#endif
#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

#include "order_book/types.h"

using NodeIndex = std::uint32_t;
inline constexpr NodeIndex kNullNode = std::numeric_limits<NodeIndex>::max();

struct OrderNode {
  Order order;
  NodeIndex prev = kNullNode;
  NodeIndex next = kNullNode;
};

class OrderPool {
public:
  explicit OrderPool(std::size_t capacity) { nodes_.reserve(capacity); }

  [[nodiscard]] NodeIndex acquire(const Order &order) {
    if (free_head_ == kNullNode) {
      nodes_.push_back(OrderNode{order});
      return static_cast<NodeIndex>(nodes_.size() - 1);
    }
    const NodeIndex index = free_head_;
    free_head_ = nodes_[index].next;
    nodes_[index] = OrderNode{order};
    return index;
  }

  void release(NodeIndex index) {
    nodes_[index].next = free_head_;
    free_head_ = index;
  }

  [[nodiscard]] OrderNode &operator[](NodeIndex index) { return nodes_[index]; }
  [[nodiscard]] const OrderNode &operator[](NodeIndex index) const { return nodes_[index]; }

private:
  std::vector<OrderNode> nodes_;
  NodeIndex free_head_ = kNullNode;
};

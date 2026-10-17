#ifndef PRICE_LEVEL_HPP
#define PRICE_LEVEL_HPP

#include "order_node.hpp"

struct PriceLevel
{
    long totalQuantity{0};
    std::size_t orderCount{0};
    OrderNode *head{nullptr};
    OrderNode *tail{nullptr};

    void addOrder(OrderNode *node)
    {
        if (node == nullptr)
        {
            return;
        }
        if (head == nullptr)
        {
            head = node;
            tail = node;
        }
        else
        {
            tail->next = node;
            node->prev = tail;
            tail = node;
        }
        totalQuantity += node->qty;
        orderCount++;
    }

    void removeOrder(OrderNode *node)
    {
        if (node == nullptr)
        {
            return;
        }

        if (node->prev != nullptr)
        {
            node->prev->next = node->next;
        }
        else
        {
            head = node->next;
        }

        if (node->next != nullptr)
        {
            node->next->prev = node->prev;
        }
        else
        {
            tail = node->prev;
        }

        totalQuantity -= node->qty;
        orderCount--;

        node->next = nullptr;
        node->prev = nullptr;
    }
};

#endif
#ifndef ORDER_NODE_HPP
#define ORDER_NODE_HPP

#include <cstdint>
#include "types.hpp"
struct OrderNode
{
    std::uint64_t id;
    std::uint64_t price;
    OrderNode *next{nullptr};
    OrderNode *prev{nullptr};
    long qty;
    Side side;
};

#endif
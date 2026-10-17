#include "OrderBookA.h"

#include <utility>

void OrderBookA::add_order(uint64_t id, Side side, double price, long qty)
{
    int ticks = priceToTicks(price);
    auto &level = (side == Side::Buy) ? bids_[ticks] : asks_[ticks];

    level.orders.push_back(qty);
    level.totalQty += qty;
    orders_[id] = OrderLocation{side, ticks, std::prev(level.orders.end())};
}

bool OrderBookA::cancel_order(uint64_t id)
{
    auto found = orders_.find(id);
    if (found == orders_.end())
    {
        return false;
    }

    OrderLocation loc = found->second;
    orders_.erase(found);

    if (loc.side == Side::Buy)
    {
        auto levelIt = bids_.find(loc.priceTicks);
        levelIt->second.totalQty -= *loc.it;
        levelIt->second.orders.erase(loc.it);
        if (levelIt->second.orders.empty())
        {
            bids_.erase(levelIt);
        }
    }
    else
    {
        auto levelIt = asks_.find(loc.priceTicks);
        levelIt->second.totalQty -= *loc.it;
        levelIt->second.orders.erase(loc.it);
        if (levelIt->second.orders.empty())
        {
            asks_.erase(levelIt);
        }
    }

    return true;
}

BBO OrderBookA::get_bbo() const
{
    BBO bbo;
    if (!bids_.empty())
    {
        bbo.has_bid = true;
        bbo.bid_price = ticksToPrice(bids_.begin()->first);
        bbo.bid_qty = bids_.begin()->second.totalQty;
    }
    if (!asks_.empty())
    {
        bbo.has_ask = true;
        bbo.ask_price = ticksToPrice(asks_.begin()->first);
        bbo.ask_qty = asks_.begin()->second.totalQty;
    }
    return bbo;
}

std::vector<Level> OrderBookA::get_bids(int depth) const
{
    std::vector<Level> result;
    result.reserve(depth > 0 ? depth : 0);
    for (auto it = bids_.begin(); it != bids_.end() && static_cast<int>(result.size()) < depth; ++it)
    {
        result.push_back(Level{ticksToPrice(it->first), it->second.totalQty});
    }
    return result;
}

std::vector<Level> OrderBookA::get_asks(int depth) const
{
    std::vector<Level> result;
    result.reserve(depth > 0 ? depth : 0);
    for (auto it = asks_.begin(); it != asks_.end() && static_cast<int>(result.size()) < depth; ++it)
    {
        result.push_back(Level{ticksToPrice(it->first), it->second.totalQty});
    }
    return result;
}

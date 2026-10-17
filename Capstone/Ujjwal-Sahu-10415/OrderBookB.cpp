#include "OrderBookB.h"

void OrderBookB::add_order(uint64_t id, Side side, double price, long qty)
{
    int ticks = priceToTicks(price);
    orders_[id] = OrderInfo{side, ticks, qty};

    auto &qtyMap = (side == Side::Buy) ? bidQty_ : askQty_;
    auto &priceSet = (side == Side::Buy) ? bidPrices_ : askPrices_;

    auto [it, inserted] = qtyMap.try_emplace(ticks, qty);
    if (!inserted)
    {
        it->second += qty;
    }
    else
    {
        priceSet.insert(ticks);
    }
}

bool OrderBookB::cancel_order(uint64_t id)
{
    auto found = orders_.find(id);
    if (found == orders_.end())
    {
        return false;
    }

    OrderInfo info = found->second;
    orders_.erase(found);

    auto &qtyMap = (info.side == Side::Buy) ? bidQty_ : askQty_;
    auto &priceSet = (info.side == Side::Buy) ? bidPrices_ : askPrices_;

    auto levelIt = qtyMap.find(info.priceTicks);
    levelIt->second -= info.qty;
    if (levelIt->second <= 0)
    {
        qtyMap.erase(levelIt);
        priceSet.erase(info.priceTicks);
    }

    return true;
}

BBO OrderBookB::get_bbo() const
{
    BBO bbo;
    if (!bidPrices_.empty())
    {
        int bestTicks = *bidPrices_.rbegin();
        bbo.has_bid = true;
        bbo.bid_price = ticksToPrice(bestTicks);
        bbo.bid_qty = bidQty_.at(bestTicks);
    }
    if (!askPrices_.empty())
    {
        int bestTicks = *askPrices_.begin();
        bbo.has_ask = true;
        bbo.ask_price = ticksToPrice(bestTicks);
        bbo.ask_qty = askQty_.at(bestTicks);
    }
    return bbo;
}

std::vector<Level> OrderBookB::get_bids(int depth) const
{
    std::vector<Level> result;
    result.reserve(depth > 0 ? depth : 0);
    for (auto it = bidPrices_.rbegin(); it != bidPrices_.rend() && static_cast<int>(result.size()) < depth; ++it)
    {
        result.push_back(Level{ticksToPrice(*it), bidQty_.at(*it)});
    }
    return result;
}

std::vector<Level> OrderBookB::get_asks(int depth) const
{
    std::vector<Level> result;
    result.reserve(depth > 0 ? depth : 0);
    for (auto it = askPrices_.begin(); it != askPrices_.end() && static_cast<int>(result.size()) < depth; ++it)
    {
        result.push_back(Level{ticksToPrice(*it), askQty_.at(*it)});
    }
    return result;
}

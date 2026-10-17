#include "OrderBookC.h"

void OrderBookC::ensureOrderCapacity(uint64_t id)
{
    if (id < orders_.size())
    {
        return;
    }
    // Manual doubling instead of orders_.resize(id + 1) directly: resize()
    // grows to exactly the requested size, so repeatedly resizing by one
    // element at a time would be O(n) per call, O(n^2) overall.
    size_t newSize = orders_.empty() ? 1 : orders_.size();
    while (newSize <= id)
    {
        newSize *= 2;
    }
    orders_.resize(newSize);
}

void OrderBookC::onLevelFilled(Side side, int idx)
{
    if (side == Side::Buy)
    {
        if (bestBidIndex_ == -1 || idx > bestBidIndex_)
        {
            bestBidIndex_ = idx;
        }
    }
    else
    {
        if (bestAskIndex_ == -1 || idx < bestAskIndex_)
        {
            bestAskIndex_ = idx;
        }
    }
}

void OrderBookC::onLevelEmptied(Side side, int idx)
{
    if (side == Side::Buy)
    {
        if (idx != bestBidIndex_)
        {
            return;
        }
        int i = idx - 1;
        while (i >= kBidLowIndex && levelQty_[i] == 0)
        {
            --i;
        }
        bestBidIndex_ = (i >= kBidLowIndex) ? i : -1;
    }
    else
    {
        if (idx != bestAskIndex_)
        {
            return;
        }
        int i = idx + 1;
        while (i <= kAskHighIndex && levelQty_[i] == 0)
        {
            ++i;
        }
        bestAskIndex_ = (i <= kAskHighIndex) ? i : -1;
    }
}

void OrderBookC::add_order(uint64_t id, Side side, double price, long qty)
{
    int idx = indexOf(priceToTicks(price));

    bool wasEmpty = (levelQty_[idx] == 0);
    levelQty_[idx] += qty;

    ensureOrderCapacity(id);
    orders_[id] = OrderSlot{true, side, idx, qty};

    if (wasEmpty)
    {
        onLevelFilled(side, idx);
    }
}

bool OrderBookC::cancel_order(uint64_t id)
{
    if (id >= orders_.size() || !orders_[id].active)
    {
        return false;
    }

    OrderSlot &slot = orders_[id];
    int idx = slot.tickIndex;
    Side side = slot.side;
    long qty = slot.qty;
    slot.active = false;

    levelQty_[idx] -= qty;
    if (levelQty_[idx] == 0)
    {
        onLevelEmptied(side, idx);
    }

    return true;
}

BBO OrderBookC::get_bbo() const
{
    BBO bbo;
    if (bestBidIndex_ != -1)
    {
        bbo.has_bid = true;
        bbo.bid_price = ticksToPrice(bestBidIndex_ + kMinTick);
        bbo.bid_qty = levelQty_[bestBidIndex_];
    }
    if (bestAskIndex_ != -1)
    {
        bbo.has_ask = true;
        bbo.ask_price = ticksToPrice(bestAskIndex_ + kMinTick);
        bbo.ask_qty = levelQty_[bestAskIndex_];
    }
    return bbo;
}

std::vector<Level> OrderBookC::get_bids(int depth) const
{
    std::vector<Level> result;
    result.reserve(depth > 0 ? depth : 0);
    if (bestBidIndex_ == -1)
    {
        return result;
    }
    for (int i = bestBidIndex_; i >= kBidLowIndex && static_cast<int>(result.size()) < depth; --i)
    {
        if (levelQty_[i] != 0)
        {
            result.push_back(Level{ticksToPrice(i + kMinTick), levelQty_[i]});
        }
    }
    return result;
}

std::vector<Level> OrderBookC::get_asks(int depth) const
{
    std::vector<Level> result;
    result.reserve(depth > 0 ? depth : 0);
    if (bestAskIndex_ == -1)
    {
        return result;
    }
    for (int i = bestAskIndex_; i <= kAskHighIndex && static_cast<int>(result.size()) < depth; ++i)
    {
        if (levelQty_[i] != 0)
        {
            result.push_back(Level{ticksToPrice(i + kMinTick), levelQty_[i]});
        }
    }
    return result;
}

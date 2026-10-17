#include "implementations.hpp"
#include <algorithm>
#include <stdexcept>

namespace orderbook
{

    void TreeBook::add_order(std::uint64_t id, Side side, double price, long qty)
    {
        if (qty <= 0)
            throw std::invalid_argument("quantity must be positive");
        if (ids_.count(id))
            throw std::invalid_argument("order ID must be unique");
        const int tick = price_to_tick(price, side);
        auto &m = levels(side);
        auto [it, inserted] = m.try_emplace(tick);
        (void)inserted;
        it->second.orders.push_back({id, qty});
        auto order = std::prev(it->second.orders.end());
        it->second.qty += qty;
        ids_.emplace(id, Locator{side, tick, order});
    }
    bool TreeBook::cancel_order(std::uint64_t id)
    {
        auto found = ids_.find(id);
        if (found == ids_.end())
            return false;
        const auto loc = found->second;
        auto &m = levels(loc.side);
        auto level = m.find(loc.tick);
        level->second.qty -= loc.order->qty;
        level->second.orders.erase(loc.order);
        if (level->second.orders.empty())
            m.erase(level);
        ids_.erase(found);
        return true;
    }
    std::vector<Level> TreeBook::read(const Levels &m, bool reverse, int depth) const
    {
        std::vector<Level> out;
        if (depth <= 0)
            return out;
        out.reserve(static_cast<std::size_t>(depth));
        if (reverse)
        {
            for (auto it = m.rbegin(); it != m.rend() && static_cast<int>(out.size()) < depth; ++it)
                out.push_back({tick_to_price(it->first), it->second.qty});
        }
        else
        {
            for (auto it = m.begin(); it != m.end() && static_cast<int>(out.size()) < depth; ++it)
                out.push_back({tick_to_price(it->first), it->second.qty});
        }
        return out;
    }
    BBO TreeBook::get_bbo() const
    {
        BBO b{};
        if (!bids_.empty())
        {
            auto i = std::prev(bids_.end());
            b.has_bid = true;
            b.bid_price = tick_to_price(i->first);
            b.bid_qty = i->second.qty;
        }
        if (!asks_.empty())
        {
            auto i = asks_.begin();
            b.has_ask = true;
            b.ask_price = tick_to_price(i->first);
            b.ask_qty = i->second.qty;
        }
        return b;
    }

    void VectorBook::add_order(std::uint64_t id, Side side, double price, long qty)
    {
        if (qty <= 0)
            throw std::invalid_argument("quantity must be positive");
        if (ids_.count(id))
            throw std::invalid_argument("order ID must be unique");
        const int tick = price_to_tick(price, side);
        auto &v = levels(side);
        auto it = std::lower_bound(v.begin(), v.end(), tick, [](const PriceLevel &a, int t)
                                   { return a.tick < t; });
        if (it == v.end() || it->tick != tick)
            it = v.insert(it, PriceLevel{tick, 0});
        it->qty += qty;
        ids_.emplace(id, OrderRef{side, tick, qty});
    }
    bool VectorBook::cancel_order(std::uint64_t id)
    {
        auto found = ids_.find(id);
        if (found == ids_.end())
            return false;
        auto ref = found->second;
        auto &v = levels(ref.side);
        auto it = std::lower_bound(v.begin(), v.end(), ref.tick, [](const PriceLevel &a, int t)
                                   { return a.tick < t; });
        it->qty -= ref.qty;
        if (it->qty == 0)
            v.erase(it);
        ids_.erase(found);
        return true;
    }
    std::vector<Level> VectorBook::read(const std::vector<PriceLevel> &v, bool reverse, int depth)
    {
        std::vector<Level> out;
        if (depth <= 0)
            return out;
        const auto n = std::min<std::size_t>(v.size(), static_cast<std::size_t>(depth));
        out.reserve(n);
        for (std::size_t i = 0; i < n; ++i)
        {
            const auto &p = reverse ? v[v.size() - 1 - i] : v[i];
            out.push_back({tick_to_price(p.tick), p.qty});
        }
        return out;
    }
    BBO VectorBook::get_bbo() const
    {
        BBO b{};
        if (!bids_.empty())
        {
            const auto &p = bids_.back();
            b.has_bid = true;
            b.bid_price = tick_to_price(p.tick);
            b.bid_qty = p.qty;
        }
        if (!asks_.empty())
        {
            const auto &p = asks_.front();
            b.has_ask = true;
            b.ask_price = tick_to_price(p.tick);
            b.ask_qty = p.qty;
        }
        return b;
    }

    void ArrayBook::add_order(std::uint64_t id, Side side, double price, long qty)
    {
        if (qty <= 0)
            throw std::invalid_argument("quantity must be positive");
        if (ids_.count(id))
            throw std::invalid_argument("order ID must be unique");
        const int tick = price_to_tick(price, side);
        auto &s = state(side, tick);
        if (s.qty == 0)
        {
            ++s.generation;
            HeapEntry e{tick, s.generation};
            if (side == Side::Buy)
                bid_heap_.push(e);
            else
                ask_heap_.push(e);
        }
        s.qty += qty;
        ids_.emplace(id, OrderRef{side, tick, qty});
    }
    bool ArrayBook::cancel_order(std::uint64_t id)
    {
        auto found = ids_.find(id);
        if (found == ids_.end())
            return false;
        auto ref = found->second;
        state(ref.side, ref.tick).qty -= ref.qty;
        ids_.erase(found);
        return true;
    }
    void ArrayBook::discard_stale() const
    {
        while (!bid_heap_.empty())
        {
            auto e = bid_heap_.top();
            const auto &s = bids_[index(e.tick)];
            if (s.qty > 0 && s.generation == e.generation)
                break;
            bid_heap_.pop();
        }
        while (!ask_heap_.empty())
        {
            auto e = ask_heap_.top();
            const auto &s = asks_[index(e.tick)];
            if (s.qty > 0 && s.generation == e.generation)
                break;
            ask_heap_.pop();
        }
    }
    BBO ArrayBook::get_bbo() const
    {
        discard_stale();
        BBO b{};
        if (!bid_heap_.empty())
        {
            auto e = bid_heap_.top();
            b.has_bid = true;
            b.bid_price = tick_to_price(e.tick);
            b.bid_qty = bids_[index(e.tick)].qty;
        }
        if (!ask_heap_.empty())
        {
            auto e = ask_heap_.top();
            b.has_ask = true;
            b.ask_price = tick_to_price(e.tick);
            b.ask_qty = asks_[index(e.tick)].qty;
        }
        return b;
    }
    std::vector<Level> ArrayBook::read(const std::array<LevelState, kSize> &a, bool reverse, int depth)
    {
        std::vector<Level> out;
        if (depth <= 0)
            return out;
        out.reserve(static_cast<std::size_t>(depth));
        int tick = reverse ? kLast : kFirst;
        const int end = reverse ? kFirst : kLast;
        const int step = reverse ? -1 : 1;
        for (; ((reverse && tick >= end) || (!reverse && tick <= end)) && static_cast<int>(out.size()) < depth; tick += step)
        {
            const auto qty = a[index(tick)].qty;
            if (qty > 0)
                out.push_back({tick_to_price(tick), qty});
        }
        return out;
    }

} // namespace orderbook

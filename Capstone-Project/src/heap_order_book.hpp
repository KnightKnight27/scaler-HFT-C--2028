#pragma once

#include "order_book.hpp"

#include <map>
#include <optional>
#include <queue>

class HeapOrderBook final : public OrderBook {
    struct Entry { Tick price; std::uint64_t generation; };
    struct MaxCompare { bool operator()(const Entry& a, const Entry& b) const { return a.price < b.price; } };
    struct MinCompare { bool operator()(const Entry& a, const Entry& b) const { return a.price > b.price; } };
    using MaxHeap = std::priority_queue<Entry, std::vector<Entry>, MaxCompare>;
    using MinHeap = std::priority_queue<Entry, std::vector<Entry>, MinCompare>;
    using Generations = std::unordered_map<Tick, std::uint64_t>;

public:
    void add_order(OrderId id, Side side, Tick price, long quantity) override {
        validate_order(side, price, quantity);
        validate_new_id(used_ids_, id);
        auto& levels = side == Side::Buy ? bids_ : asks_;
        if (levels.find(price) == levels.end()) {
            if (side == Side::Buy) bid_heap_.push({price, ++bid_generations_[price]});
            else ask_heap_.push({price, ++ask_generations_[price]});
        }
        levels[price] += quantity;
        orders_.emplace(id, StoredOrder{side, price, quantity});
        used_ids_.insert(id);
    }

    bool cancel_order(OrderId id) override {
        const auto it = orders_.find(id);
        if (it == orders_.end()) return false;
        auto& levels = it->second.side == Side::Buy ? bids_ : asks_;
        const auto level = levels.find(it->second.price);
        level->second -= it->second.quantity;
        if (level->second == 0) levels.erase(level);
        orders_.erase(it);
        return true;
    }

    BBO get_bbo() const override {
        BBO result;
        const auto bid = live_top(bid_heap_, bids_, bid_generations_);
        const auto ask = live_top(ask_heap_, asks_, ask_generations_);
        if (bid) { result.has_bid = true; result.bid_price = bid->price; result.bid_quantity = bids_.at(bid->price); }
        if (ask) { result.has_ask = true; result.ask_price = ask->price; result.ask_quantity = asks_.at(ask->price); }
        return result;
    }

    std::vector<Level> get_bids(int depth = 10) const override { return levels_from(bid_heap_, bids_, bid_generations_, depth); }
    std::vector<Level> get_asks(int depth = 10) const override { return levels_from(ask_heap_, asks_, ask_generations_, depth); }

private:
    template <typename Heap>
    static std::optional<Entry> live_top(Heap heap, const std::map<Tick, long>& levels, const Generations& generations) {
        while (!heap.empty() && (levels.find(heap.top().price) == levels.end() || generations.at(heap.top().price) != heap.top().generation)) heap.pop();
        if (heap.empty()) return std::nullopt;
        return heap.top();
    }

    template <typename Heap>
    static std::vector<Level> levels_from(Heap heap, const std::map<Tick, long>& levels, const Generations& generations, int depth) {
        std::vector<Level> result;
        while (depth-- > 0 && !heap.empty()) {
            while (!heap.empty() && (levels.find(heap.top().price) == levels.end() || generations.at(heap.top().price) != heap.top().generation)) heap.pop();
            if (heap.empty()) break;
            const Tick price = heap.top().price;
            result.push_back({price, levels.at(price)});
            heap.pop();
        }
        return result;
    }

    std::map<Tick, long> bids_;
    std::map<Tick, long> asks_;
    MaxHeap bid_heap_;
    MinHeap ask_heap_;
    Generations bid_generations_;
    Generations ask_generations_;
    std::unordered_map<OrderId, StoredOrder> orders_;
    std::unordered_set<OrderId> used_ids_;
};

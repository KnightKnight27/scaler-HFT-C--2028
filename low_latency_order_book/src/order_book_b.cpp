#include "order_book_b.hpp"

void OrderBookB::clean_bids() const {
    while (!bid_pq_.empty()) {
        const auto& top = bid_pq_.top();
        auto it = orders_.find(top.id);
        // If order is cancelled, or if the price level aggregated quantity is <= 0, pop it
        if (it == orders_.end() || !it->second.active || bid_quantities_.at(top.price_tick) <= 0) {
            bid_pq_.pop();
        } else {
            break;
        }
    }
}

void OrderBookB::clean_asks() const {
    while (!ask_pq_.empty()) {
        const auto& top = ask_pq_.top();
        auto it = orders_.find(top.id);
        if (it == orders_.end() || !it->second.active || ask_quantities_.at(top.price_tick) <= 0) {
            ask_pq_.pop();
        } else {
            break;
        }
    }
}

void OrderBookB::add_order(uint64_t id, Side side, double price, long qty) {
    int price_tick = static_cast<int>(std::round(price * 100.0));

    orders_[id] = {side, price_tick, qty, true};

    if (side == Side::Buy) {
        bid_quantities_[price_tick] += qty;
        bid_pq_.push({price_tick, id});
    } else {
        ask_quantities_[price_tick] += qty;
        ask_pq_.push({price_tick, id});
    }
}

bool OrderBookB::cancel_order(uint64_t id) {
    auto it = orders_.find(id);
    if (it == orders_.end() || !it->second.active) {
        return false;
    }

    it->second.active = false; // Mark inactive (lazy deletion)
    const auto& order = it->second;

    if (order.side == Side::Buy) {
        auto q_it = bid_quantities_.find(order.price_tick);
        if (q_it != bid_quantities_.end()) {
            q_it->second -= order.qty;
            if (q_it->second < 0) q_it->second = 0;
        }
    } else {
        auto q_it = ask_quantities_.find(order.price_tick);
        if (q_it != ask_quantities_.end()) {
            q_it->second -= order.qty;
            if (q_it->second < 0) q_it->second = 0;
        }
    }

    return true;
}

BBO OrderBookB::get_bbo() const {
    BBO bbo{false, 0.0, 0, false, 0.0, 0};

    clean_bids();
    clean_asks();

    if (!bid_pq_.empty()) {
        bbo.has_bid = true;
        int tick = bid_pq_.top().price_tick;
        bbo.bid_price = tick_to_price(tick);
        bbo.bid_qty = bid_quantities_.at(tick);
    }

    if (!ask_pq_.empty()) {
        bbo.has_ask = true;
        int tick = ask_pq_.top().price_tick;
        bbo.ask_price = tick_to_price(tick);
        bbo.ask_qty = ask_quantities_.at(tick);
    }

    return bbo;
}

std::vector<Level> OrderBookB::get_bids(int depth) const {
    clean_bids();
    std::vector<Level> levels;
    std::unordered_map<int, long> active_levels = bid_quantities_;
    std::vector<std::pair<int, long>> sorted_levels;
    for (const auto& [tick, qty] : active_levels) {
        if (qty > 0) {
            sorted_levels.push_back({tick, qty});
        }
    }
    std::sort(sorted_levels.begin(), sorted_levels.end(), [](auto& a, auto& b) {
        return a.first > b.first;
    });

    int count = 0;
    for (const auto& [tick, qty] : sorted_levels) {
        if (count >= depth) break;
        levels.push_back({tick_to_price(tick), qty});
        count++;
    }

    return levels;
}

std::vector<Level> OrderBookB::get_asks(int depth) const {
    clean_asks();
    std::vector<Level> levels;

    std::unordered_map<int, long> active_levels = ask_quantities_;
    
    std::vector<std::pair<int, long>> sorted_levels;
    for (const auto& [tick, qty] : active_levels) {
        if (qty > 0) {
            sorted_levels.push_back({tick, qty});
        }
    }
    std::sort(sorted_levels.begin(), sorted_levels.end(), [](auto& a, auto& b) {
        return a.first < b.first;
    });

    int count = 0;
    for (const auto& [tick, qty] : sorted_levels) {
        if (count >= depth) break;
        levels.push_back({tick_to_price(tick), qty});
        count++;
    }

    return levels;
}
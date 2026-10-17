#include "impl_b_pqueue.h"

void OrderBookB::clean_bids() const {
    while (!bidHeap.empty()) {
        auto it = bidQty.find(bidHeap.top());
        if (it == bidQty.end() || it->second == 0)
            bidHeap.pop();
        else
            break;
    }
}

void OrderBookB::clean_asks() const {
    while (!askHeap.empty()) {
        auto it = askQty.find(askHeap.top());
        if (it == askQty.end() || it->second == 0)
            askHeap.pop();
        else
            break;
    }
}

void OrderBookB::add_order(uint64_t id, Side side, double price, long qty) {
    int tick = price_to_tick(price);
    orders[id] = OrderRec{ side, tick, qty };
    if (side == Side::Buy) {
        long &q = bidQty[tick];
        if (q == 0) bidHeap.push(tick);
        q += qty;
    } else {
        long &q = askQty[tick];
        if (q == 0) askHeap.push(tick);
        q += qty;
    }
}

bool OrderBookB::cancel_order(uint64_t id) {
    auto it = orders.find(id);
    if (it == orders.end())
        return false;

    OrderRec rec = it->second;
    if (rec.side == Side::Buy)
        bidQty[rec.tick] -= rec.qty;
    else
        askQty[rec.tick] -= rec.qty;
    orders.erase(it);
    return true;
}

BBO OrderBookB::get_bbo() const {
    BBO out{};
    clean_bids();
    clean_asks();
    if (!bidHeap.empty()) {
        int t = bidHeap.top();
        out.has_bid = true;
        out.bid_price = tick_to_price(t);
        out.bid_qty = bidQty.at(t);
    }
    if (!askHeap.empty()) {
        int t = askHeap.top();
        out.has_ask = true;
        out.ask_price = tick_to_price(t);
        out.ask_qty = askQty.at(t);
    }
    return out;
}

std::vector<Level> OrderBookB::get_bids(int depth) const {
    std::vector<Level> out;
    clean_bids();
    std::priority_queue<int> copy = bidHeap;
    int last = -1;
    while (!copy.empty() && (int)out.size() < depth) {
        int t = copy.top();
        copy.pop();
        if (t == last) continue;
        auto qit = bidQty.find(t);
        if (qit == bidQty.end() || qit->second == 0) continue;
        last = t;
        out.push_back(Level{ tick_to_price(t), qit->second });
    }
    return out;
}

std::vector<Level> OrderBookB::get_asks(int depth) const {
    std::vector<Level> out;
    clean_asks();
    std::priority_queue<int, std::vector<int>, std::greater<int>> copy = askHeap;
    int last = -1;
    while (!copy.empty() && (int)out.size() < depth) {
        int t = copy.top();
        copy.pop();
        if (t == last) continue;
        auto qit = askQty.find(t);
        if (qit == askQty.end() || qit->second == 0) continue;
        last = t;
        out.push_back(Level{ tick_to_price(t), qit->second });
    }
    return out;
}

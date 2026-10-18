#include "orderbook_b.h"

using namespace std;

OrderBookB::OrderBookB() {}

OrderBookB::~OrderBookB() {
    for (auto& p : level_by_tick) {
        delete p.second;
    }
}

void OrderBookB::drop_zeros() const {
    while (!bid_heap.empty() && bid_heap.top()->qty <= 0) {
        PLevel* dead = bid_heap.top();
        bid_heap.pop();
        level_by_tick.erase(dead->tick);
        delete dead;
    }
    while (!ask_heap.empty() && ask_heap.top()->qty <= 0) {
        PLevel* dead = ask_heap.top();
        ask_heap.pop();
        level_by_tick.erase(dead->tick);
        delete dead;
    }
}

void OrderBookB::add_order(uint64_t id, Side side, double price, long qty) {
    long t = to_ticks(price);
    PLevel* lv;
    auto found = level_by_tick.find(t);
    if (found != level_by_tick.end()) {
        lv = found->second;       // price level already exists, add on to it
        lv->qty += qty;
    } else {
        lv = new PLevel{t, qty};
        level_by_tick[t] = lv;
        if (side == Side::Buy) {
            bid_heap.push(lv);
        } else {
            ask_heap.push(lv);
        }
    }
    orders[id] = make_pair(lv, qty);
}

bool OrderBookB::cancel_order(uint64_t id) {
    auto it = orders.find(id);
    if (it == orders.end()) {
        return false;
    }
    // lazy delete: just take the qty off the level, no heap fix up
    it->second.first->qty -= it->second.second;
    orders.erase(it);
    return true;
}

BBO OrderBookB::get_bbo() const {
    drop_zeros();
    BBO b;
    if (!bid_heap.empty()) {
        b.has_bid = true;
        b.bid_price = to_price(bid_heap.top()->tick);
        b.bid_qty = bid_heap.top()->qty;
    }
    if (!ask_heap.empty()) {
        b.has_ask = true;
        b.ask_price = to_price(ask_heap.top()->tick);
        b.ask_qty = ask_heap.top()->qty;
    }
    return b;
}

vector<Level> OrderBookB::get_bids(int depth) const {
    vector<Level> out;
    if (depth <= 0) return out;
    vector<PLevel*> put_back;

    while (!bid_heap.empty() && (int)out.size() < depth) {
        PLevel* top = bid_heap.top();
        bid_heap.pop();
        if (top->qty <= 0) {
            level_by_tick.erase(top->tick);
            delete top;
        } else {
            out.push_back(Level{to_price(top->tick), top->qty});
            put_back.push_back(top);
        }
    }
    for (auto p : put_back) {
        bid_heap.push(p);   // put the live ones back
    }
    return out;
}

vector<Level> OrderBookB::get_asks(int depth) const {
    vector<Level> out;
    if (depth <= 0) return out;
    vector<PLevel*> put_back;

    while (!ask_heap.empty() && (int)out.size() < depth) {
        PLevel* top = ask_heap.top();
        ask_heap.pop();
        if (top->qty <= 0) {
            level_by_tick.erase(top->tick);
            delete top;
        } else {
            out.push_back(Level{to_price(top->tick), top->qty});
            put_back.push_back(top);
        }
    }
    for (auto p : put_back) {
        ask_heap.push(p);
    }
    return out;
}

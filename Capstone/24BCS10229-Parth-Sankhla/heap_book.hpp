#pragma once

#include <functional>
#include <map>
#include <queue>
#include <unordered_map>

#include "common.hpp"

// B: priority_queue of prices with lazy delete
class HeapBook {
public:
    void add_order(uint64_t id, Side side, int price, long qty) {
        if (qty <= 0 || !valid_price(side, price) || orders.count(id))
            return;

        orders[id] = {id, side, price, qty, next_ts++};
        if (side == Side::Buy) {
            if (bid_qty.count(price) == 0)
                bid_heap.push(price);
            bid_qty[price] += qty;
        } else {
            if (ask_qty.count(price) == 0)
                ask_heap.push(price);
            ask_qty[price] += qty;
        }
    }

    bool cancel_order(uint64_t id) {
        std::map<uint64_t, Order>::iterator it = orders.find(id);
        if (it == orders.end())
            return false;

        Order o = it->second;
        orders.erase(it);

        if (o.side == Side::Buy) {
            bid_qty[o.price] -= o.qty;
            if (bid_qty[o.price] == 0) {
                bid_qty.erase(o.price);
                while (!bid_heap.empty() && bid_qty.count(bid_heap.top()) == 0)
                    bid_heap.pop();
            }
        } else {
            ask_qty[o.price] -= o.qty;
            if (ask_qty[o.price] == 0) {
                ask_qty.erase(o.price);
                while (!ask_heap.empty() && ask_qty.count(ask_heap.top()) == 0)
                    ask_heap.pop();
            }
        }
        return true;
    }

    BBO get_bbo() const {
        BBO b = {false, 0, 0, false, 0, 0};
        if (!bid_heap.empty()) {
            b.has_bid = true;
            b.bid_price = bid_heap.top();
            b.bid_qty = bid_qty.at(b.bid_price);
        }
        if (!ask_heap.empty()) {
            b.has_ask = true;
            b.ask_price = ask_heap.top();
            b.ask_qty = ask_qty.at(b.ask_price);
        }
        return b;
    }

    std::vector<Level> get_bids(int depth = 10) const {
        std::vector<Level> out;
        std::priority_queue<int> copy = bid_heap;
        int last = -1;
        while (!copy.empty() && (int)out.size() < depth) {
            int p = copy.top();
            copy.pop();
            if (p == last || bid_qty.count(p) == 0)
                continue;
            out.push_back({p, bid_qty.at(p)});
            last = p;
        }
        return out;
    }

    std::vector<Level> get_asks(int depth = 10) const {
        std::vector<Level> out;
        std::priority_queue<int, std::vector<int>, std::greater<int>> copy = ask_heap;
        int last = -1;
        while (!copy.empty() && (int)out.size() < depth) {
            int p = copy.top();
            copy.pop();
            if (p == last || ask_qty.count(p) == 0)
                continue;
            out.push_back({p, ask_qty.at(p)});
            last = p;
        }
        return out;
    }

private:
    std::priority_queue<int> bid_heap;
    std::priority_queue<int, std::vector<int>, std::greater<int>> ask_heap;
    std::unordered_map<int, long> bid_qty;
    std::unordered_map<int, long> ask_qty;
    std::map<uint64_t, Order> orders;
    uint64_t next_ts = 0;
};

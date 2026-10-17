#pragma once

#include "order_book_a.hpp"
#include "order_book_b.hpp"
#include "order_book_c.hpp"
#include <iostream>
#include <random>
#include <vector>
#include <cassert>

inline bool compare_bbo(const BBO& b1, const BBO& b2) {
    return b1.has_bid == b2.has_bid &&
           (!b1.has_bid || (b1.bid_price == b2.bid_price && b1.bid_qty == b2.bid_qty)) &&
           b1.has_ask == b2.has_ask &&
           (!b1.has_ask || (b1.ask_price == b2.ask_price && b1.ask_qty == b2.ask_qty));
}

inline bool compare_levels(const std::vector<Level>& l1, const std::vector<Level>& l2) {
    if (l1.size() != l2.size()) return false;
    for (size_t i = 0; i < l1.size(); ++i) {
        if (l1[i].price != l2[i].price || l1[i].qty != l2[i].qty) return false;
    }
    return true;
}

inline void run_correctness_test() {
    std::cout << "Running correctness test...\n";

    OrderBookA bookA;
    OrderBookB bookB;
    OrderBookC bookC;

    std::mt19937 rng(42);
    std::uniform_int_distribution<int> op_dist(0, 99); 
    std::uniform_int_distribution<int> side_dist(0, 1);
    std::uniform_int_distribution<long> qty_dist(1, 100);
    std::uniform_int_distribution<int> bid_price_dist(5000, 9999);
    std::uniform_int_distribution<int> ask_price_dist(10001, 15000);

    std::vector<uint64_t> live_order_ids;
    uint64_t next_id = 1;

    for (int i = 0; i < 50000; ++i) {
        int op = op_dist(rng);

        if (op < 70 || live_order_ids.empty()) {
            uint64_t id = next_id++;
            Side side = (side_dist(rng) == 0) ? Side::Buy : Side::Sell;
            double price = (side == Side::Buy) ? (bid_price_dist(rng) * 0.01) : (ask_price_dist(rng) * 0.01);
            long qty = qty_dist(rng);

            bookA.add_order(id, side, price, qty);
            bookB.add_order(id, side, price, qty);
            bookC.add_order(id, side, price, qty);

            live_order_ids.push_back(id);
        } else if (op < 90) {
            std::uniform_int_distribution<size_t> idx_dist(0, live_order_ids.size() - 1);
            size_t idx = idx_dist(rng);
            uint64_t id = live_order_ids[idx];

            live_order_ids[idx] = live_order_ids.back();
            live_order_ids.pop_back();

            bookA.cancel_order(id);
            bookB.cancel_order(id);
            bookC.cancel_order(id);
        } else {
            BBO bboA = bookA.get_bbo();
            BBO bboB = bookB.get_bbo();
            BBO bboC = bookC.get_bbo();

            assert(compare_bbo(bboA, bboB) && "BBO mismatch between A and B!");
            assert(compare_bbo(bboA, bboC) && "BBO mismatch between A and C!");

            auto bidsA = bookA.get_bids(10);
            auto bidsB = bookB.get_bids(10);
            auto bidsC = bookC.get_bids(10);
            assert(compare_levels(bidsA, bidsB) && "Bid levels mismatch between A and B!");
            assert(compare_levels(bidsA, bidsC) && "Bid levels mismatch between A and C!");

            auto asksA = bookA.get_asks(10);
            auto asksB = bookB.get_asks(10);
            auto asksC = bookC.get_asks(10);
            assert(compare_levels(asksA, asksB) && "Ask levels mismatch between A and B!");
            assert(compare_levels(asksA, asksC) && "Ask levels mismatch between A and C!");
        }
    }

    std::cout << "Implementation A: PASS\n";
    std::cout << "Implementation B: PASS\n";
    std::cout << "Implementation C: PASS\n";
    std::cout << "All implementations produce identical results.\n";
}
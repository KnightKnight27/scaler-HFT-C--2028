
#include "order_book.h"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

bool same_level(const Level& a, const Level& b) {
    return std::abs(a.price - b.price) < 1e-9 &&
           a.qty == b.qty;
}

bool same_bbo(const BBO& a, const BBO& b) {
    return a.has_bid == b.has_bid &&
           a.has_ask == b.has_ask &&
           (!a.has_bid ||
            (std::abs(a.bid_price - b.bid_price) < 1e-9 &&
             a.bid_qty == b.bid_qty)) &&
           (!a.has_ask ||
            (std::abs(a.ask_price - b.ask_price) < 1e-9 &&
             a.ask_qty == b.ask_qty));
}

bool same_levels(const std::vector<Level>& a,
                 const std::vector<Level>& b) {
    if (a.size() != b.size()) {
        return false;
    }

    for (std::size_t i = 0; i < a.size(); ++i) {
        if (!same_level(a[i], b[i])) {
            return false;
        }
    }

    return true;
}

bool compare_books(const OrderBook& a, const OrderBook& b,
                   int operation) {
    if (!same_bbo(a.get_bbo(), b.get_bbo())) {
        std::cerr << "BBO mismatch after operation "
                  << operation << '\n';
        return false;
    }

    if (!same_levels(a.get_bids(10), b.get_bids(10))) {
        std::cerr << "Bid depth mismatch after operation "
                  << operation << '\n';
        return false;
    }

    if (!same_levels(a.get_asks(10), b.get_asks(10))) {
        std::cerr << "Ask depth mismatch after operation "
                  << operation << '\n';
        return false;
    }

    return true;
}

int main() {
    auto a = make_map_order_book();
    auto b = make_heap_order_book();
    auto c = make_array_order_book();

    std::mt19937 rng(123456);

    std::uniform_int_distribution<int> operation_dist(0, 9);
    std::uniform_int_distribution<int> side_dist(0, 1);
    std::uniform_int_distribution<int> bid_dist(5000, 9999);
    std::uniform_int_distribution<int> ask_dist(10001, 15000);
    std::uniform_int_distribution<int> qty_dist(1, 100);

    std::vector<uint64_t> live_ids;
    uint64_t next_id = 1;

    constexpr int TOTAL_OPERATIONS = 20000;

    for (int i = 1; i <= TOTAL_OPERATIONS; ++i) {
        int operation = operation_dist(rng);

        
        if (operation < 7 || live_ids.empty()) {
            Side side = side_dist(rng) == 0
                            ? Side::Buy
                            : Side::Sell;

            int ticks = side == Side::Buy
                            ? bid_dist(rng)
                            : ask_dist(rng);

            double price = ticks / 100.0;
            long qty = qty_dist(rng);
            uint64_t id = next_id++;

            a->add_order(id, side, price, qty);
            b->add_order(id, side, price, qty);
            c->add_order(id, side, price, qty);

            live_ids.push_back(id);
        } else if (operation < 9) {
            std::uniform_int_distribution<std::size_t> pick(
                0, live_ids.size() - 1);

            std::size_t index = pick(rng);
            uint64_t id = live_ids[index];

            bool result_a = a->cancel_order(id);
            bool result_b = b->cancel_order(id);
            bool result_c = c->cancel_order(id);

            if (result_a != result_b || result_a != result_c) {
                std::cerr << "Cancellation mismatch at operation "
                          << i << '\n';
                return 1;
            }

            
            live_ids[index] = live_ids.back();
            live_ids.pop_back();
        } else {
            
            a->get_bbo();
            b->get_bbo();
            c->get_bbo();
        }

        
        if (!compare_books(*a, *b, i) ||
            !compare_books(*a, *c, i)) {
            std::cerr << "Random seed: 123456\n";
            return 1;
        }
    }

   
    if (!same_levels(a->get_bids(0), b->get_bids(0)) ||
        !same_levels(a->get_asks(-1), c->get_asks(-1))) {
        std::cerr << "Non-positive depth mismatch\n";
        return 1;
    }

    std::cout << "All implementations agree across "
              << TOTAL_OPERATIONS << " operations.\n";

    return 0;
}

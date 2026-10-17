#include "OrderBookMap.hpp"
#include "OrderBookVector.hpp"
#include "OrderBookArray.hpp"
#include <iostream>
#include <vector>
#include <random>
#include <cmath>

bool compare_bbo(const BBO& a, const BBO& b) {
    return a == b;
}

bool compare_levels(const std::vector<Level>& a, const std::vector<Level>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (!(a[i] == b[i])) return false;
    }
    return true;
}

template <typename BookType>
bool run_edge_case_tests(BookType& book, const std::string& name) {
    // 1. Empty book
    BBO bbo = book.get_bbo();
    if (bbo.has_bid || bbo.has_ask) {
        std::cerr << "[" << name << "] Failed: Empty book BBO has_bid or has_ask is true\n";
        return false;
    }
    if (!book.get_bids(10).empty() || !book.get_asks(10).empty()) {
        std::cerr << "[" << name << "] Failed: Empty book get_bids/get_asks not empty\n";
        return false;
    }

    // 2. Cancel non-existent order
    if (book.cancel_order(999999)) {
        std::cerr << "[" << name << "] Failed: Cancelling non-existent order returned true\n";
        return false;
    }

    // 3. Add single bid
    book.add_order(1, Side::Buy, 99.50, 100);
    bbo = book.get_bbo();
    if (!bbo.has_bid || bbo.bid_price != 99.50 || bbo.bid_qty != 100 || bbo.has_ask) {
        std::cerr << "[" << name << "] Failed: Single bid BBO incorrect\n";
        return false;
    }

    // 4. Add single ask
    book.add_order(2, Side::Sell, 100.50, 200);
    bbo = book.get_bbo();
    if (!bbo.has_ask || bbo.ask_price != 100.50 || bbo.ask_qty != 200) {
        std::cerr << "[" << name << "] Failed: Single ask BBO incorrect\n";
        return false;
    }

    // 5. Multiple orders at same price
    book.add_order(3, Side::Buy, 99.50, 50);
    bbo = book.get_bbo();
    if (bbo.bid_qty != 150) {
        std::cerr << "[" << name << "] Failed: Multiple orders same price quantity mismatch\n";
        return false;
    }

    // 6. Partial cancel at price level
    if (!book.cancel_order(1)) {
        std::cerr << "[" << name << "] Failed: Cancelling order 1 returned false\n";
        return false;
    }
    bbo = book.get_bbo();
    if (bbo.bid_qty != 50) {
        std::cerr << "[" << name << "] Failed: Quantity after partial cancel mismatch\n";
        return false;
    }

    // 7. Double cancel
    if (book.cancel_order(1)) {
        std::cerr << "[" << name << "] Failed: Double cancel returned true\n";
        return false;
    }

    // 8. Full cancel at price level
    if (!book.cancel_order(3)) {
        std::cerr << "[" << name << "] Failed: Cancelling order 3 returned false\n";
        return false;
    }
    bbo = book.get_bbo();
    if (bbo.has_bid) {
        std::cerr << "[" << name << "] Failed: Bid level not removed after full cancel\n";
        return false;
    }

    // Clean up ask
    book.cancel_order(2);

    // 9. Insufficient depth test: add 3 levels, ask for 10
    book.add_order(10, Side::Buy, 90.00, 10);
    book.add_order(11, Side::Buy, 91.00, 20);
    book.add_order(12, Side::Buy, 92.00, 30);
    auto bids = book.get_bids(10);
    if (bids.size() != 3) {
        std::cerr << "[" << name << "] Failed: Depth request size mismatch, expected 3, got " << bids.size() << "\n";
        return false;
    }
    if (bids[0].price != 92.00 || bids[1].price != 91.00 || bids[2].price != 90.00) {
        std::cerr << "[" << name << "] Failed: Depth levels ordering incorrect\n";
        return false;
    }

    // Clean up
    book.cancel_order(10);
    book.cancel_order(11);
    book.cancel_order(12);

    return true;
}

bool run_cross_verification(int num_operations = 100000) {
    OrderBookMap map_book;
    OrderBookVector vec_book;
    OrderBookArray arr_book;

    std::mt19937 rng(42);
    std::uniform_int_distribution<int> op_dist(0, 99);
    std::uniform_int_distribution<int> side_dist(0, 1);
    std::uniform_int_distribution<long> qty_dist(1, 100);
    std::uniform_int_distribution<int> bid_dist(5000, 9999);
    std::uniform_int_distribution<int> ask_dist(10001, 15000);

    std::vector<uint64_t> live_ids;
    uint64_t next_id = 1;

    for (int i = 0; i < num_operations; ++i) {
        int roll = op_dist(rng);

        if (roll < 70 || live_ids.empty()) { // Add 70%
            uint64_t id = next_id++;
            Side side = (side_dist(rng) == 0) ? Side::Buy : Side::Sell;
            int tick = (side == Side::Buy) ? bid_dist(rng) : ask_dist(rng);
            double price = tick * 0.01;
            long qty = qty_dist(rng);

            map_book.add_order(id, side, price, qty);
            vec_book.add_order(id, side, price, qty);
            arr_book.add_order(id, side, price, qty);
            live_ids.push_back(id);
        } else if (roll < 90) { // Cancel 20%
            std::uniform_int_distribution<size_t> idx_dist(0, live_ids.size() - 1);
            size_t idx = idx_dist(rng);
            uint64_t id_to_cancel = live_ids[idx];
            live_ids[idx] = live_ids.back();
            live_ids.pop_back();

            bool c_map = map_book.cancel_order(id_to_cancel);
            bool c_vec = vec_book.cancel_order(id_to_cancel);
            bool c_arr = arr_book.cancel_order(id_to_cancel);

            if (c_map != c_vec || c_vec != c_arr) {
                std::cerr << "Mismatch in cancel_order result at op " << i << "\n";
                return false;
            }
        } else { // BBO query 10%
            BBO bbo_map = map_book.get_bbo();
            BBO bbo_vec = vec_book.get_bbo();
            BBO bbo_arr = arr_book.get_bbo();

            if (!compare_bbo(bbo_map, bbo_vec) || !compare_bbo(bbo_vec, bbo_arr)) {
                std::cerr << "Mismatch in get_bbo at op " << i << "\n";
                return false;
            }
        }

        // Periodic depth check every 100 ops
        if ((i + 1) % 100 == 0) {
            auto bids_map = map_book.get_bids(10);
            auto bids_vec = vec_book.get_bids(10);
            auto bids_arr = arr_book.get_bids(10);

            if (!compare_levels(bids_map, bids_vec) || !compare_levels(bids_vec, bids_arr)) {
                std::cerr << "Mismatch in get_bids(10) at op " << i << "\n";
                return false;
            }

            auto asks_map = map_book.get_asks(10);
            auto asks_vec = vec_book.get_asks(10);
            auto asks_arr = arr_book.get_asks(10);

            if (!compare_levels(asks_map, asks_vec) || !compare_levels(asks_vec, asks_arr)) {
                std::cerr << "Mismatch in get_asks(10) at op " << i << "\n";
                return false;
            }
        }
    }

    return true;
}

int main() {
    std::cout << "Running correctness test...\n\n";

    OrderBookMap map_book;
    OrderBookVector vec_book;
    OrderBookArray arr_book;

    bool pass_a = run_edge_case_tests(map_book, "Implementation A");
    bool pass_b = run_edge_case_tests(vec_book, "Implementation B");
    bool pass_c = run_edge_case_tests(arr_book, "Implementation C");

    std::cout << "Implementation A: " << (pass_a ? "PASS" : "FAIL") << "\n";
    std::cout << "Implementation B: " << (pass_b ? "PASS" : "FAIL") << "\n";
    std::cout << "Implementation C: " << (pass_c ? "PASS" : "FAIL") << "\n\n";

    if (!pass_a || !pass_b || !pass_c) {
        std::cerr << "Edge case tests failed!\n";
        return 1;
    }

    std::cout << "Running cross-implementation verification on 100,000 mixed operations...\n";
    bool cross_pass = run_cross_verification(100000);
    if (!cross_pass) {
        std::cerr << "Cross-verification FAILED!\n";
        return 1;
    }

    std::cout << "All implementations produce identical results.\n";
    return 0;
}

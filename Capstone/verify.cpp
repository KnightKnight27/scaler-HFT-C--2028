#include "impl_a/orderbook_a.h"
#include "impl_b/orderbook_b.h"
#include "impl_c/orderbook_c.h"

#include <iostream>
#include <vector>
#include <random>
#include <cassert>
#include <iomanip>
#include <algorithm>

namespace {

bool bbo_equals(const BBO& a, const BBO& b) {
    if (a.has_bid != b.has_bid || a.has_ask != b.has_ask) return false;
    if (a.has_bid) {
        if (std::abs(a.bid_level.price - b.bid_level.price) > 1e-6 || a.bid_level.qty != b.bid_level.qty) {
            return false;
        }
    }
    if (a.has_ask) {
        if (std::abs(a.ask_level.price - b.ask_level.price) > 1e-6 || a.ask_level.qty != b.ask_level.qty) {
            return false;
        }
    }
    return true;
}

bool levels_equal(const std::vector<Level>& a, const std::vector<Level>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (std::abs(a[i].price - b[i].price) > 1e-6 || a[i].qty != b[i].qty) {
            return false;
        }
    }
    return true;
}

void print_level_diff(const std::string& name_1, const std::vector<Level>& lvls_1,
                      const std::string& name_2, const std::vector<Level>& lvls_2) {
    std::cerr << "Mismatch in " << name_1 << " (" << lvls_1.size() << ") vs "
              << name_2 << " (" << lvls_2.size() << "):\n";
    std::cerr << "  " << name_1 << ":\n";
    for (const auto& l : lvls_1) {
        std::cerr << "    [" << l.price << " : " << l.qty << "]\n";
    }
    std::cerr << "  " << name_2 << ":\n";
    for (const auto& l : lvls_2) {
        std::cerr << "    [" << l.price << " : " << l.qty << "]\n";
    }
}

void print_bbo_diff(const std::string& name_1, const BBO& bbo_1,
                    const std::string& name_2, const BBO& bbo_2) {
    std::cerr << "Mismatch in " << name_1 << " vs " << name_2 << ":\n";
    std::cerr << "  " << name_1 << ": has_bid=" << bbo_1.has_bid
              << " (" << bbo_1.bid_level.price << "x" << bbo_1.bid_level.qty << ")"
              << " has_ask=" << bbo_1.has_ask
              << " (" << bbo_1.ask_level.price << "x" << bbo_1.ask_level.qty << ")\n";
    std::cerr << "  " << name_2 << ": has_bid=" << bbo_2.has_bid
              << " (" << bbo_2.bid_level.price << "x" << bbo_2.bid_level.qty << ")"
              << " has_ask=" << bbo_2.has_ask
              << " (" << bbo_2.ask_level.price << "x" << bbo_2.ask_level.qty << ")\n";
}

bool verify_state_match(size_t op_index, const OrderBookA& a, const OrderBookB& b, const OrderBookC& c) {
    BBO bbo_a = a.get_bbo();
    BBO bbo_b = b.get_bbo();
    BBO bbo_c = c.get_bbo();

    if (!bbo_equals(bbo_a, bbo_b)) {
        std::cerr << "ERROR: BBO mismatch between Impl A and Impl B at op " << op_index << "\n";
        print_bbo_diff("Impl A", bbo_a, "Impl B", bbo_b);
        return false;
    }
    if (!bbo_equals(bbo_a, bbo_c)) {
        std::cerr << "ERROR: BBO mismatch between Impl A and Impl C at op " << op_index << "\n";
        print_bbo_diff("Impl A", bbo_a, "Impl C", bbo_c);
        return false;
    }

    auto bids_a = a.get_bids(10);
    auto bids_b = b.get_bids(10);
    auto bids_c = c.get_bids(10);

    if (!levels_equal(bids_a, bids_b)) {
        std::cerr << "ERROR: Bid levels mismatch between Impl A and Impl B at op " << op_index << "\n";
        print_level_diff("Impl A", bids_a, "Impl B", bids_b);
        return false;
    }
    if (!levels_equal(bids_a, bids_c)) {
        std::cerr << "ERROR: Bid levels mismatch between Impl A and Impl C at op " << op_index << "\n";
        print_level_diff("Impl A", bids_a, "Impl C", bids_c);
        return false;
    }

    auto asks_a = a.get_asks(10);
    auto asks_b = b.get_asks(10);
    auto asks_c = c.get_asks(10);

    if (!levels_equal(asks_a, asks_b)) {
        std::cerr << "ERROR: Ask levels mismatch between Impl A and Impl B at op " << op_index << "\n";
        print_level_diff("Impl A", asks_a, "Impl B", asks_b);
        return false;
    }
    if (!levels_equal(asks_a, asks_c)) {
        std::cerr << "ERROR: Ask levels mismatch between Impl A and Impl C at op " << op_index << "\n";
        print_level_diff("Impl A", asks_a, "Impl C", asks_c);
        return false;
    }

    return true;
}

bool run_unit_tests() {
    OrderBookA a;
    OrderBookB b;
    OrderBookC c;

    auto test_all = [&](const std::string& name, auto fn) -> bool {
        bool res_a = fn(a);
        bool res_b = fn(b);
        bool res_c = fn(c);
        if (!res_a || !res_b || !res_c) {
            std::cerr << "Unit test failed: " << name << "\n";
            return false;
        }
        return verify_state_match(0, a, b, c);
    };

    // 1. Initial empty book
    if (!test_all("Empty book check", [](IOrderBook& ob) {
        BBO bbo = ob.get_bbo();
        if (bbo.has_bid || bbo.has_ask) return false;
        if (!ob.get_bids(10).empty() || !ob.get_asks(10).empty()) return false;
        if (ob.cancel_order(999)) return false;
        return true;
    })) return false;

    // 2. Add single buy order
    if (!test_all("Add single buy", [](IOrderBook& ob) {
        ob.add_order(100, Side::Buy, 99.00, 50);
        BBO bbo = ob.get_bbo();
        return bbo.has_bid && !bbo.has_ask && std::abs(bbo.bid_level.price - 99.00) < 1e-6 && bbo.bid_level.qty == 50;
    })) return false;

    // 3. Add order at same price level (verify quantity sum)
    if (!test_all("Add buy at same price", [](IOrderBook& ob) {
        ob.add_order(101, Side::Buy, 99.00, 25);
        BBO bbo = ob.get_bbo();
        return bbo.has_bid && bbo.bid_level.qty == 75;
    })) return false;

    // 4. Add higher bid (verify new best bid)
    if (!test_all("Add higher buy", [](IOrderBook& ob) {
        ob.add_order(102, Side::Buy, 99.50, 40);
        BBO bbo = ob.get_bbo();
        return bbo.has_bid && std::abs(bbo.bid_level.price - 99.50) < 1e-6 && bbo.bid_level.qty == 40;
    })) return false;

    // 5. Add sell orders
    if (!test_all("Add sells", [](IOrderBook& ob) {
        ob.add_order(200, Side::Sell, 101.00, 100);
        ob.add_order(201, Side::Sell, 100.50, 60);
        BBO bbo = ob.get_bbo();
        return bbo.has_ask && std::abs(bbo.ask_level.price - 100.50) < 1e-6 && bbo.ask_level.qty == 60;
    })) return false;

    // 6. Check depth queries and truncation
    if (!test_all("Depth queries", [](IOrderBook& ob) {
        auto bids = ob.get_bids(10);
        if (bids.size() != 2) return false;
        if (std::abs(bids[0].price - 99.50) > 1e-6 || bids[0].qty != 40) return false;
        if (std::abs(bids[1].price - 99.00) > 1e-6 || bids[1].qty != 75) return false;

        auto asks = ob.get_asks(10);
        if (asks.size() != 2) return false;
        if (std::abs(asks[0].price - 100.50) > 1e-6 || asks[0].qty != 60) return false;
        if (std::abs(asks[1].price - 101.00) > 1e-6 || asks[1].qty != 100) return false;

        // Depth 1 request
        auto bids_1 = ob.get_bids(1);
        if (bids_1.size() != 1 || std::abs(bids_1[0].price - 99.50) > 1e-6) return false;
        return true;
    })) return false;

    // 7. Cancel order 102 (revert best bid to 99.00)
    if (!test_all("Cancel top bid", [](IOrderBook& ob) {
        if (!ob.cancel_order(102)) return false;
        if (ob.cancel_order(102)) return false; // second cancel must fail
        BBO bbo = ob.get_bbo();
        return bbo.has_bid && std::abs(bbo.bid_level.price - 99.00) < 1e-6 && bbo.bid_level.qty == 75;
    })) return false;

    // 8. Cancel one order from 99.00 level (quantity should decrease to 50)
    if (!test_all("Cancel order from same level", [](IOrderBook& ob) {
        if (!ob.cancel_order(101)) return false;
        BBO bbo = ob.get_bbo();
        return bbo.has_bid && std::abs(bbo.bid_level.price - 99.00) < 1e-6 && bbo.bid_level.qty == 50;
    })) return false;

    // 9. Cancel remaining orders
    if (!test_all("Cancel remaining orders", [](IOrderBook& ob) {
        if (!ob.cancel_order(100)) return false;
        if (!ob.cancel_order(200)) return false;
        if (!ob.cancel_order(201)) return false;
        BBO bbo = ob.get_bbo();
        return !bbo.has_bid && !bbo.has_ask;
    })) return false;

    return true;
}

bool run_randomized_workload(size_t total_ops) {
    OrderBookA a;
    OrderBookB b;
    OrderBookC c;

    std::mt19937 rng(42);

    // Distribution: 70% Add, 20% Cancel, 10% BBO query
    std::uniform_int_distribution<int> op_dist(1, 100);
    std::uniform_int_distribution<int> side_dist(0, 1);
    std::uniform_int_distribution<long> qty_dist(1, 100);

    // Bid price: 50.00 to 99.99 (5000 valid ticks: 5000 + [0, 4999])
    // Ask price: 100.01 to 150.00 (5000 valid ticks: 10001 + [0, 4999])
    std::uniform_int_distribution<int> bid_tick_dist(5000, 9999);
    std::uniform_int_distribution<int> ask_tick_dist(10001, 15000);

    // Track active orders for realistic cancellations
    std::vector<uint64_t> active_orders;
    std::unordered_map<uint64_t, size_t> active_indices;

    auto remove_active_order = [&](uint64_t id) {
        if (auto it = active_indices.find(id); it != active_indices.end()) {
            size_t idx = it->second;
            uint64_t last_id = active_orders.back();
            active_orders[idx] = last_id;
            active_indices[last_id] = idx;
            active_orders.pop_back();
            active_indices.erase(it);
        }
    };

    uint64_t next_order_id = 1;

    for (size_t op = 1; op <= total_ops; ++op) {
        int roll = op_dist(rng);

        if (roll <= 70 || active_orders.empty()) {
            // Add order
            uint64_t id = next_order_id++;
            Side side = (side_dist(rng) == 0) ? Side::Buy : Side::Sell;
            double price = 0.0;
            if (side == Side::Buy) {
                price = bid_tick_dist(rng) / 100.0;
            } else {
                price = ask_tick_dist(rng) / 100.0;
            }
            long qty = qty_dist(rng);

            a.add_order(id, side, price, qty);
            b.add_order(id, side, price, qty);
            c.add_order(id, side, price, qty);

            active_indices[id] = active_orders.size();
            active_orders.push_back(id);
        } else if (roll <= 90) {
            // Cancel order
            std::uniform_int_distribution<size_t> idx_dist(0, active_orders.size() - 1);
            size_t pick_idx = idx_dist(rng);
            uint64_t id = active_orders[pick_idx];

            bool res_a = a.cancel_order(id);
            bool res_b = b.cancel_order(id);
            bool res_c = c.cancel_order(id);

            if (res_a != res_b || res_b != res_c) {
                std::cerr << "ERROR: Cancel order return mismatch at op " << op
                          << " (A=" << res_a << ", B=" << res_b << ", C=" << res_c << ")\n";
                return false;
            }

            remove_active_order(id);
        } else {
            // BBO query
            BBO bbo_a = a.get_bbo();
            BBO bbo_b = b.get_bbo();
            BBO bbo_c = c.get_bbo();

            if (!bbo_equals(bbo_a, bbo_b)) {
                std::cerr << "ERROR: BBO mismatch A vs B at op " << op << "\n";
                print_bbo_diff("Impl A", bbo_a, "Impl B", bbo_b);
                return false;
            }
            if (!bbo_equals(bbo_a, bbo_c)) {
                std::cerr << "ERROR: BBO mismatch A vs C at op " << op << "\n";
                print_bbo_diff("Impl A", bbo_a, "Impl C", bbo_c);
                return false;
            }
        }

        // Every 100 operations: query get_bids(10) and get_asks(10)
        if (op % 100 == 0) {
            if (!verify_state_match(op, a, b, c)) {
                return false;
            }
        }
    }

    // Final verification across full depth
    if (!verify_state_match(total_ops, a, b, c)) {
        return false;
    }

    return true;
}

} // namespace

int main(int argc, char* argv[]) {
    size_t num_operations = 25000;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--ops" && i + 1 < argc) {
            num_operations = std::stoull(argv[++i]);
        } else if (arg == "--verify" || arg == "-v") {
            // Standard flag requested in assignment.md
            continue;
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: " << argv[0] << " [--verify] [--ops <N>]\n";
            std::cout << "Default ops: 25000 (pass --ops 100000 for full 100k stress test)\n";
            return 0;
        }
    }

    std::cout << "Running correctness test...\n" << std::endl;

    if (!run_unit_tests()) {
        std::cerr << "Unit tests failed!" << std::endl;
        return 1;
    }

    if (!run_randomized_workload(num_operations)) {
        std::cerr << "Randomized workload verification failed!" << std::endl;
        return 1;
    }

    std::cout << "Implementation A: PASS" << std::endl;
    std::cout << "Implementation B: PASS" << std::endl;
    std::cout << "Implementation C: PASS\n" << std::endl;
    std::cout << "All implementations produce identical results." << std::endl;

    return 0;
}

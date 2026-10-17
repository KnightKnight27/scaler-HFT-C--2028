#include "workload.hpp"
#include "orderbook_a.hpp"
#include "orderbook_b.hpp"
#include "orderbook_c.hpp"

#include <iostream>
#include <random>

std::vector<WorkloadOp> generate_workload(size_t num_ops) {
    std::vector<WorkloadOp> ops;
    ops.reserve(num_ops);

    std::mt19937 rng(42);
    std::uniform_int_distribution<int> op_dist(1, 100);
    std::uniform_int_distribution<int> side_dist(0, 1);
    std::uniform_int_distribution<int32_t> bid_tick_dist(MIN_BID_TICK, MAX_BID_TICK);
    std::uniform_int_distribution<int32_t> ask_tick_dist(MIN_ASK_TICK, MAX_ASK_TICK);
    std::uniform_int_distribution<long> qty_dist(1, 100);

    std::vector<uint64_t> active_orders;
    active_orders.reserve(num_ops);
    uint64_t next_order_id = 1;

    for (size_t i = 0; i < num_ops; ++i) {
        int roll = op_dist(rng);

        if (roll <= 70 || active_orders.empty()) {
            // 70% Add Order (or fallback to Add if no active orders exist to cancel)
            Side s = (side_dist(rng) == 0) ? Side::Buy : Side::Sell;
            int32_t tick = (s == Side::Buy) ? bid_tick_dist(rng) : ask_tick_dist(rng);
            long qty = qty_dist(rng);
            uint64_t id = next_order_id++;

            active_orders.push_back(id);
            ops.push_back(WorkloadOp{OpType::Add, s, id, tick_to_price(tick), qty});
        } else if (roll <= 90) {
            // 20% Cancel Order (valid live order picked uniformly via swap-and-pop)
            std::uniform_int_distribution<size_t> idx_dist(0, active_orders.size() - 1);
            size_t idx = idx_dist(rng);
            uint64_t cancel_id = active_orders[idx];
            active_orders[idx] = active_orders.back();
            active_orders.pop_back();

            ops.push_back(WorkloadOp{OpType::Cancel, Side::Buy, cancel_id, 0.0, 0});
        } else {
            // 10% GetBBO
            ops.push_back(WorkloadOp{OpType::GetBBO, Side::Buy, 0, 0.0, 0});
        }
    }

    return ops;
}

template <typename Book>
bool run_edge_case_tests(const char* name) {
    Book book;

    // 1. Empty book BBO and Depth checks
    BBO empty_bbo = book.get_bbo();
    if (empty_bbo.has_bid || empty_bbo.has_ask) {
        std::cerr << "[FAIL] " << name << ": Empty book has_bid/has_ask not false\n";
        return false;
    }
    if (!book.get_bids(10).empty() || !book.get_asks(10).empty()) {
        std::cerr << "[FAIL] " << name << ": Empty book depth not empty\n";
        return false;
    }

    // 2. Cancel non-existent order ID (including out-of-pool ID)
    if (book.cancel_order(999999) || book.cancel_order(5000000)) {
        std::cerr << "[FAIL] " << name << ": Cancelling non-existent order returned true\n";
        return false;
    }

    // 3. Add multiple orders at the same price level (aggregation check)
    book.add_order(1, Side::Buy, 99.50, 100);
    book.add_order(2, Side::Buy, 99.50, 250);
    book.add_order(3, Side::Buy, 98.00, 500);
    book.add_order(4, Side::Sell, 100.50, 300);

    BBO bbo1 = book.get_bbo();
    if (!bbo1.has_bid || price_to_tick(bbo1.bid_price) != 9950 || bbo1.bid_qty != 350 ||
        !bbo1.has_ask || price_to_tick(bbo1.ask_price) != 10050 || bbo1.ask_qty != 300) {
        std::cerr << "[FAIL] " << name << ": Aggregation or BBO mismatch\n";
        return false;
    }

    // 4. Request depth = 10 when fewer than 10 levels exist
    auto bids = book.get_bids(10);
    if (bids.size() != 2 || price_to_tick(bids[0].price) != 9950 || bids[0].qty != 350 ||
        price_to_tick(bids[1].price) != 9800 || bids[1].qty != 500) {
        std::cerr << "[FAIL] " << name << ": Partial depth < 10 check failed\n";
        return false;
    }

    // 5. Partial level cancellation and complete level depletion
    if (!book.cancel_order(1)) return false; // 99.50 drops to 250
    if (book.cancel_order(1)) {              // Double cancel must return false
        std::cerr << "[FAIL] " << name << ": Double cancel returned true\n";
        return false;
    }
    if (!book.cancel_order(2)) return false; // 99.50 drops to 0 and must be removed

    BBO bbo2 = book.get_bbo();
    if (!bbo2.has_bid || price_to_tick(bbo2.bid_price) != 9800 || bbo2.bid_qty != 500) {
        std::cerr << "[FAIL] " << name << ": Level removal did not update BBO to next best level\n";
        return false;
    }

    // 6. Deplete remaining orders and verify book returns to empty state
    if (!book.cancel_order(3) || !book.cancel_order(4)) return false;
    BBO bbo3 = book.get_bbo();
    if (bbo3.has_bid || bbo3.has_ask) {
        std::cerr << "[FAIL] " << name << ": Book not empty after cancelling all orders\n";
        return false;
    }

    return true;
}

bool run_verification() {
    std::cout << "==========================================================" << std::endl;
    std::cout << "1. Running Mandatory Edge-Case Unit Tests..." << std::endl;
    if (!run_edge_case_tests<OrderBookA>("OrderBookA") ||
        !run_edge_case_tests<OrderBookB>("OrderBookB") ||
        !run_edge_case_tests<OrderBookC>("OrderBookC")) {
        return false;
    }
    std::cout << "   -> Edge-Case Tests (Empty BBO, Invalid/Double Cancel, Level Depletion, Partial Depth): PASS\n";

    std::cout << "2. Generating 1,000,000 Deterministic Operations (mt19937 seed=42)..." << std::endl;
    auto ops = generate_workload(1000000);

    OrderBookA bookA;
    OrderBookB bookB;
    OrderBookC bookC;

    std::cout << "3. Cross-Verifying Implementations A, B, and C across 1,000,000 ops..." << std::endl;

    int64_t checksumA = 0, checksumB = 0, checksumC = 0;

    for (size_t i = 0; i < ops.size(); ++i) {
        const auto& op = ops[i];
        if (op.type == OpType::Add) {
            bookA.add_order(op.id, op.side, op.price, op.qty);
            bookB.add_order(op.id, op.side, op.price, op.qty);
            bookC.add_order(op.id, op.side, op.price, op.qty);
        } else if (op.type == OpType::Cancel) {
            bool rA = bookA.cancel_order(op.id);
            bool rB = bookB.cancel_order(op.id);
            bool rC = bookC.cancel_order(op.id);
            if (rA != rB || rA != rC) {
                std::cerr << "[FAIL] Cancel mismatch at op " << i << "\n";
                return false;
            }
            checksumA += rA ? 1 : 0;
            checksumB += rB ? 1 : 0;
            checksumC += rC ? 1 : 0;
        } else {
            BBO bA = bookA.get_bbo();
            BBO bB = bookB.get_bbo();
            BBO bC = bookC.get_bbo();
            if (!(bA == bB) || !(bA == bC)) {
                std::cerr << "[FAIL] BBO mismatch at op " << i << "\n";
                return false;
            }
            checksumA += bA.bid_qty + bA.ask_qty;
            checksumB += bB.bid_qty + bB.ask_qty;
            checksumC += bC.bid_qty + bC.ask_qty;
        }

        if ((i + 1) % 100 == 0) {
            auto bidsA = bookA.get_bids(10);
            auto bidsB = bookB.get_bids(10);
            auto bidsC = bookC.get_bids(10);

            auto asksA = bookA.get_asks(10);
            auto asksB = bookB.get_asks(10);
            auto asksC = bookC.get_asks(10);

            if (bidsA != bidsB || bidsA != bidsC || asksA != asksB || asksA != asksC) {
                std::cerr << "[FAIL] Top-10 Depth mismatch at op " << i << "\n";
                return false;
            }

            if (!bidsA.empty()) {
                checksumA += bidsA[0].qty;
                checksumB += bidsB[0].qty;
                checksumC += bidsC[0].qty;
            }
            if (!asksA.empty()) {
                checksumA += asksA[0].qty;
                checksumB += asksB[0].qty;
                checksumC += asksC[0].qty;
            }
        }
    }

    BBO finalA = bookA.get_bbo();
    BBO finalB = bookB.get_bbo();
    BBO finalC = bookC.get_bbo();

    if (!(finalA == finalB) || !(finalA == finalC) ||
        checksumA != checksumB || checksumA != checksumC) {
        std::cerr << "[FAIL] Final state mismatch!\n";
        return false;
    }

    std::cout << "Running correctness test...\n";
    std::cout << "Implementation A: PASS\n";
    std::cout << "Implementation B: PASS\n";
    std::cout << "Implementation C: PASS\n";
    std::cout << "All implementations produce identical results.\n";
    std::cout << "Final BBO: Bid: " << finalA.bid_price << " x " << finalA.bid_qty
              << " | Ask: " << finalA.ask_price << " x " << finalA.ask_qty << "\n";
    std::cout << "Checksum: " << checksumA << "\n";
    std::cout << "==========================================================" << std::endl;

    return true;
}
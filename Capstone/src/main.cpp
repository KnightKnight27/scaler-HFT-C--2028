#include <iostream>
#include <iomanip>
#include <vector>
#include <string>
#include <random>
#include <chrono>
#include <algorithm>
#include <memory>

#include "order_book.hpp"
#include "order_book_map.hpp"
#include "order_book_vector.hpp"
#include "order_book_indexed.hpp"

// Operations generated for the benchmark workload
struct Op {
    int type; // 0: Add, 1: Cancel, 2: GetBBO, 3: GetDepth
    uint64_t id;
    Side side;
    double price;
    long qty;
};

// Generates 1,000,000 deterministic operations with seed 42
std::vector<Op> generate_workload(size_t total_ops = 1000000, uint32_t seed = 42) {
    std::vector<Op> ops;
    ops.reserve(total_ops);

    std::mt19937 rng(seed);
    std::vector<uint64_t> live_orders;
    live_orders.reserve(total_ops);

    uint64_t next_id = 1;

    for (size_t i = 0; i < total_ops; i++) {
        // Every 100 operations, query depth
        if (i > 0 && i % 100 == 0) {
            ops.push_back({3, 0, Side::Buy, 0.0, 0});
        }

        int roll = rng() % 100;

        if (roll < 70 || live_orders.empty()) {
            // 70% Add Order
            uint64_t id = next_id++;
            Side side = (rng() % 2 == 0) ? Side::Buy : Side::Sell;
            double price = 0.0;
            if (side == Side::Buy) {
                // Bid range [50.00, 99.99]
                price = 50.00 + (rng() % 5000) * 0.01;
            } else {
                // Ask range [100.01, 150.00]
                price = 100.01 + (rng() % 5000) * 0.01;
            }
            price = std::round(price * 100.0) / 100.0;
            long qty = 1 + (rng() % 100);

            ops.push_back({0, id, side, price, qty});
            live_orders.push_back(id);
        } else if (roll < 90) {
            // 20% Cancel live order
            size_t idx = rng() % live_orders.size();
            uint64_t cancel_id = live_orders[idx];

            // Swap with back and pop
            live_orders[idx] = live_orders.back();
            live_orders.pop_back();

            ops.push_back({1, cancel_id, Side::Buy, 0.0, 0});
        } else {
            // 10% BBO query
            ops.push_back({2, 0, Side::Buy, 0.0, 0});
        }
    }

    return ops;
}

// Factory to create chosen implementation
std::unique_ptr<OrderBook> create_book(char impl) {
    if (impl == 'A' || impl == 'a') return std::make_unique<OrderBookMap>();
    if (impl == 'B' || impl == 'b') return std::make_unique<OrderBookVector>();
    if (impl == 'C' || impl == 'c') return std::make_unique<OrderBookIndexed>();
    return nullptr;
}

// Verify that all 3 implementations give identical results
bool run_verification() {
    std::cout << "Running correctness test...\n" << std::endl;

    // Test edge cases first
    auto check_basics = [](OrderBook& book, const std::string& name) {
        // 1. Empty book
        BBO b = book.get_bbo();
        if (b.has_bid || b.has_ask) {
            std::cerr << "Failed empty book test on " << name << "\n";
            return false;
        }

        // 2. Unknown cancel
        if (book.cancel_order(12345)) {
            std::cerr << "Failed unknown cancel test on " << name << "\n";
            return false;
        }

        // 3. Insufficient depth
        book.add_order(1, Side::Buy, 60.00, 10);
        book.add_order(2, Side::Buy, 61.00, 20);
        book.add_order(3, Side::Buy, 62.00, 30);
        auto bids = book.get_bids(10);
        if (bids.size() != 3 || bids[0].price != 62.00) {
            std::cerr << "Failed depth test on " << name << "\n";
            return false;
        }

        // Clean up
        book.cancel_order(1);
        book.cancel_order(2);
        book.cancel_order(3);
        if (book.cancel_order(1)) {
            std::cerr << "Failed duplicate cancel test on " << name << "\n";
            return false;
        }

        return true;
    };

    OrderBookMap bA;
    OrderBookVector bB;
    OrderBookIndexed bC;

    if (!check_basics(bA, "Implementation A")) return false;
    if (!check_basics(bB, "Implementation B")) return false;
    if (!check_basics(bC, "Implementation C")) return false;

    // Run same 100,000 ops through all 3
    auto bookA = std::make_unique<OrderBookMap>();
    auto bookB = std::make_unique<OrderBookVector>();
    auto bookC = std::make_unique<OrderBookIndexed>();

    auto workload = generate_workload(100000, 42);
    std::cout << "Testing with workload of " << workload.size() << " operations..." << std::endl;

    for (size_t i = 0; i < workload.size(); i++) {
        const auto& op = workload[i];

        if (op.type == 0) {
            bookA->add_order(op.id, op.side, op.price, op.qty);
            bookB->add_order(op.id, op.side, op.price, op.qty);
            bookC->add_order(op.id, op.side, op.price, op.qty);
        } else if (op.type == 1) {
            bool rA = bookA->cancel_order(op.id);
            bool rB = bookB->cancel_order(op.id);
            bool rC = bookC->cancel_order(op.id);
            if (rA != rB || rA != rC) {
                std::cerr << "Mismatch on cancel at step " << i << "\n";
                return false;
            }
        } else if (op.type == 2) {
            BBO bboA = bookA->get_bbo();
            BBO bboB = bookB->get_bbo();
            BBO bboC = bookC->get_bbo();
            if (!(bboA == bboB) || !(bboA == bboC)) {
                std::cerr << "Mismatch on BBO at step " << i << "\n";
                return false;
            }
        } else if (op.type == 3) {
            auto bidsA = bookA->get_bids(10);
            auto bidsB = bookB->get_bids(10);
            auto bidsC = bookC->get_bids(10);
            if (bidsA != bidsB || bidsA != bidsC) {
                std::cerr << "Mismatch on get_bids at step " << i << "\n";
                return false;
            }

            auto asksA = bookA->get_asks(10);
            auto asksB = bookB->get_asks(10);
            auto asksC = bookC->get_asks(10);
            if (asksA != asksB || asksA != asksC) {
                std::cerr << "Mismatch on get_asks at step " << i << "\n";
                return false;
            }
        }
    }

    std::cout << "Implementation A: PASS\n";
    std::cout << "Implementation B: PASS\n";
    std::cout << "Implementation C: PASS\n";
    std::cout << "\nAll implementations produce identical results." << std::endl;
    return true;
}

struct RunStats {
    double total_ms;
    double ns_add;
    double ns_cancel;
    double ns_bbo;
    uint64_t checksum;
    BBO final_bbo;
};

// Measures one benchmark pass
RunStats run_pass(char impl, const std::vector<Op>& ops) {
    auto book = create_book(impl);

    uint64_t checksum = 0;
    long add_cnt = 0, cancel_cnt = 0, bbo_cnt = 0;
    long long add_ns = 0, cancel_ns = 0, bbo_ns = 0;

    auto start_all = std::chrono::steady_clock::now();

    for (const auto& op : ops) {
        if (op.type == 0) {
            auto t0 = std::chrono::steady_clock::now();
            book->add_order(op.id, op.side, op.price, op.qty);
            auto t1 = std::chrono::steady_clock::now();
            add_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
            add_cnt++;
        } else if (op.type == 1) {
            auto t0 = std::chrono::steady_clock::now();
            bool ok = book->cancel_order(op.id);
            auto t1 = std::chrono::steady_clock::now();
            cancel_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
            cancel_cnt++;
            if (!ok) checksum += 1;
        } else if (op.type == 2) {
            auto t0 = std::chrono::steady_clock::now();
            BBO b = book->get_bbo();
            auto t1 = std::chrono::steady_clock::now();
            bbo_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
            bbo_cnt++;

            if (b.has_bid) checksum = checksum * 31 + (long)(b.bid_price * 100) + b.bid_qty;
            if (b.has_ask) checksum = checksum * 31 + (long)(b.ask_price * 100) + b.ask_qty;
        } else if (op.type == 3) {
            auto bids = book->get_bids(10);
            auto asks = book->get_asks(10);
            for (const auto& l : bids) checksum = checksum * 37 + (long)(l.price * 100) + l.qty;
            for (const auto& l : asks) checksum = checksum * 41 + (long)(l.price * 100) + l.qty;
        }
    }

    auto end_all = std::chrono::steady_clock::now();
    double total_ms = std::chrono::duration<double, std::milli>(end_all - start_all).count();

    BBO final_bbo = book->get_bbo();
    if (final_bbo.has_bid) checksum += (long)(final_bbo.bid_price * 100) + final_bbo.bid_qty;
    if (final_bbo.has_ask) checksum += (long)(final_bbo.ask_price * 100) + final_bbo.ask_qty;

    RunStats stats;
    stats.total_ms = total_ms;
    stats.ns_add = add_cnt ? (double)add_ns / add_cnt : 0.0;
    stats.ns_cancel = cancel_cnt ? (double)cancel_ns / cancel_cnt : 0.0;
    stats.ns_bbo = bbo_cnt ? (double)bbo_ns / bbo_cnt : 0.0;
    stats.checksum = checksum;
    stats.final_bbo = final_bbo;

    return stats;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cout << "Usage:\n"
                  << "  " << argv[0] << " --verify\n"
                  << "  " << argv[0] << " --impl [A|B|C]\n";
        return 1;
    }

    std::string arg = argv[1];

    if (arg == "--verify") {
        return run_verification() ? 0 : 1;
    }

    if (arg == "--impl") {
        if (argc < 3) {
            std::cerr << "Please specify implementation: A, B, or C\n";
            return 1;
        }

        char impl = std::toupper(argv[2][0]);
        if (impl != 'A' && impl != 'B' && impl != 'C') {
            std::cerr << "Invalid implementation. Choose A, B, or C\n";
            return 1;
        }

        std::cout << "========================================\n";
        std::cout << " Benchmarking Implementation " << impl << "\n";
        std::cout << "========================================\n";

        std::cout << "Generating 1,000,000 operations (seed 42)..." << std::endl;
        auto workload = generate_workload(1000000, 42);
        std::cout << "Running 5 benchmark iterations...\n" << std::endl;

        std::vector<RunStats> runs;
        for (int r = 1; r <= 5; r++) {
            std::cout << "Run " << r << " / 5 ... " << std::flush;
            auto s = run_pass(impl, workload);
            runs.push_back(s);
            std::cout << std::fixed << std::setprecision(2)
                      << s.total_ms << " ms "
                      << "(add: " << s.ns_add << " ns, "
                      << "cancel: " << s.ns_cancel << " ns, "
                      << "bbo: " << s.ns_bbo << " ns)\n";
        }

        // Sort by total time to find median (element index 2 out of 5)
        std::sort(runs.begin(), runs.end(), [](const RunStats& a, const RunStats& b) {
            return a.total_ms < b.total_ms;
        });
        const auto& med = runs[2];

        std::cout << "\n----------------------------------------\n";
        std::cout << " Median Results (Implementation " << impl << "):\n";
        std::cout << "----------------------------------------\n";
        std::cout << std::fixed << std::setprecision(2);
        std::cout << "Total execution time : " << med.total_ms << " ms\n";
        std::cout << "Average ns/add       : " << med.ns_add << " ns\n";
        std::cout << "Average ns/cancel    : " << med.ns_cancel << " ns\n";
        std::cout << "Average ns/bbo       : " << med.ns_bbo << " ns\n";
        std::cout << "----------------------------------------\n";

        std::cout << "Final BBO:\n";
        if (med.final_bbo.has_bid) {
            std::cout << "Bid: " << med.final_bbo.bid_price << " x " << med.final_bbo.bid_qty << "\n";
        } else {
            std::cout << "Bid: none\n";
        }
        if (med.final_bbo.has_ask) {
            std::cout << "Ask: " << med.final_bbo.ask_price << " x " << med.final_bbo.ask_qty << "\n";
        } else {
            std::cout << "Ask: none\n";
        }

        std::cout << "Checksum: " << med.checksum << "\n";
        std::cout << "========================================\n";

        return 0;
    }

    return 1;
}

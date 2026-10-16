#include <iostream>
#include <chrono>
#include <random>
#include <vector>
#include <string>
#include <algorithm>
#include "order_book_a.hpp"
#include "order_book_b.hpp"
#include "order_book_c.hpp"

struct Operation {
    int type; // 0: add, 1: cancel, 2: bbo
    uint64_t id;
    Side side;
    double price;
    long qty;
};

template <typename BookType>
void run_benchmark_for_impl(const std::string& impl_name) {
    std::mt19937 rng(42);
    std::uniform_int_distribution<int> op_dist(0, 99);
    std::uniform_int_distribution<int> side_dist(0, 1);
    std::uniform_int_distribution<long> qty_dist(1, 100);
    std::uniform_int_distribution<int> bid_price_dist(5000, 9999);
    std::uniform_int_distribution<int> ask_price_dist(10001, 15000);

    const int TOTAL_OPS = 1'000'000;
    
    std::vector<Operation> ops;
    ops.reserve(TOTAL_OPS);
    
    std::vector<uint64_t> active_ids;
    uint64_t next_id = 1;

    for (int i = 0; i < TOTAL_OPS; ++i) {
        int op_type = op_dist(rng);
        if (op_type < 70 || active_ids.empty()) {
            Side side = (side_dist(rng) == 0) ? Side::Buy : Side::Sell;
            int tick = (side == Side::Buy) ? bid_price_dist(rng) : ask_price_dist(rng);
            double price = static_cast<double>(tick) / 100.0;
            long qty = qty_dist(rng);
            
            ops.push_back({0, next_id, side, price, qty});
            active_ids.push_back(next_id);
            next_id++;
        } else if (op_type < 90 && !active_ids.empty()) {
            std::uniform_int_distribution<size_t> idx_dist(0, active_ids.size() - 1);
            size_t idx = idx_dist(rng);
            uint64_t cancel_id = active_ids[idx];
            active_ids[idx] = active_ids.back();
            active_ids.pop_back();

            ops.push_back({1, cancel_id, Side::Buy, 0.0, 0});
        } else {
            ops.push_back({2, 0, Side::Buy, 0.0, 0});
        }
    }

    std::vector<double> run_times_ms;
    long long total_adds = 0, total_cancels = 0, total_bbos = 0;
    long long add_ns = 0, cancel_ns = 0, bbo_ns = 0;
    BBO final_bbo{false, 0, 0, false, 0, 0};

    std::cout << "Running Implementation " << impl_name << " Benchmark (5 runs)..." << std::endl;

    for (int run = 0; run < 5; ++run) {
        BookType book;
        long long add_count = 0, cancel_count = 0, bbo_count = 0;
        long long add_duration = 0, cancel_duration = 0, bbo_duration = 0;

        auto start_total = std::chrono::steady_clock::now();

        for (int i = 0; i < TOTAL_OPS; ++i) {
            const auto& op = ops[i];

            if (op.type == 0) {
                auto start = std::chrono::steady_clock::now();
                book.add_order(op.id, op.side, op.price, op.qty);
                auto end = std::chrono::steady_clock::now();
                add_duration += std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
                add_count++;
            } else if (op.type == 1) {
                auto start = std::chrono::steady_clock::now();
                book.cancel_order(op.id);
                auto end = std::chrono::steady_clock::now();
                cancel_duration += std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
                cancel_count++;
            } else {
                auto start = std::chrono::steady_clock::now();
                volatile auto bbo = book.get_bbo();
                auto end = std::chrono::steady_clock::now();
                bbo_duration += std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
                bbo_count++;
            }

            if (i % 100 == 0) {
                volatile auto bids = book.get_bids(10);
                volatile auto asks = book.get_asks(10);
            }
        }

        auto end_total = std::chrono::steady_clock::now();
        double elapsed_ms = std::chrono::duration<double, std::milli>(end_total - start_total).count();
        run_times_ms.push_back(elapsed_ms);

        if (run == 4) {
            total_adds = add_count;
            total_cancels = cancel_count;
            total_bbos = bbo_count;
            add_ns = add_duration;
            cancel_ns = cancel_duration;
            bbo_ns = bbo_duration;
            final_bbo = book.get_bbo();
        }
    }

    std::sort(run_times_ms.begin(), run_times_ms.end());
    double median_time_ms = run_times_ms[2];

    std::cout << "\n=== Implementation " << impl_name << " Benchmark Results (Median of 5 Runs) ===" << std::endl;
    std::cout << "Total Execution Time: " << median_time_ms << " ms" << std::endl;
    std::cout << "Avg ns / add_order:   " << static_cast<double>(add_ns) / total_adds << " ns" << std::endl;
    std::cout << "Avg ns / cancel_order:" << static_cast<double>(cancel_ns) / total_cancels << " ns" << std::endl;
    std::cout << "Avg ns / get_bbo:     " << static_cast<double>(bbo_ns) / total_bbos << " ns" << std::endl;
    
    long checksum = static_cast<long>(final_bbo.bid_price * 100) + static_cast<long>(final_bbo.ask_price * 100) 
                  + final_bbo.bid_qty + final_bbo.ask_qty;
    std::cout << "\nFinal BBO:" << std::endl;
    std::cout << "Bid: " << final_bbo.bid_price << " x " << final_bbo.bid_qty << std::endl;
    std::cout << "Ask: " << final_bbo.ask_price << " x " << final_bbo.ask_qty << std::endl;
    std::cout << "Checksum: " << checksum << std::endl;
}

void run_verify() {
    std::cout << "Running correctness test for Implementations A, B & C..." << std::endl;
    
    OrderBookA bookA;
    bookA.add_order(1, Side::Buy, 98.50, 10);
    bookA.add_order(2, Side::Sell, 101.50, 20);
    auto bboA = bookA.get_bbo();

    OrderBookB bookB;
    bookB.add_order(1, Side::Buy, 98.50, 10);
    bookB.add_order(2, Side::Sell, 101.50, 20);
    auto bboB = bookB.get_bbo();

    OrderBookC bookC;
    bookC.add_order(1, Side::Buy, 98.50, 10);
    bookC.add_order(2, Side::Sell, 101.50, 20);
    auto bboC = bookC.get_bbo();

    if (bboA.bid_price == bboB.bid_price && bboA.ask_price == bboB.ask_price &&
        bboA.bid_price == bboC.bid_price && bboA.ask_price == bboC.ask_price) {
        std::cout << "Implementation A: PASS" << std::endl;
        std::cout << "Implementation B: PASS" << std::endl;
        std::cout << "Implementation C: PASS" << std::endl;
        std::cout << "All implementations agree!" << std::endl;
    } else {
        std::cout << "Correctness mismatch between implementations!" << std::endl;
    }
}

int main(int argc, char* argv[]) {
    if (argc > 1 && std::string(argv[1]) == "--verify") {
        run_verify();
    } else if (argc > 2 && std::string(argv[1]) == "--impl") {
        std::string impl = argv[2];
        if (impl == "A") {
            run_benchmark_for_impl<OrderBookA>("A");
        } else if (impl == "B") {
            run_benchmark_for_impl<OrderBookB>("B");
        }
        else if (impl == "C") {
            run_benchmark_for_impl<OrderBookC>("C");
        }
        else {
            std::cout << "Unknown implementation. Use A, B, or C." << std::endl;
        }
    } else {
        std::cout << "Usage: ./bench [--verify | --impl A|B|C]" << std::endl;
    }
    return 0;
}
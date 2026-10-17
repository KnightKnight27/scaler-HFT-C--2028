#include "order_book_a.hpp"
#include "order_book_b.hpp"
#include "order_book_c.hpp"
#include "verify.hpp"
#include <iostream>
#include <chrono>
#include <random>
#include <string>
#include <vector>
#include <algorithm>

template <typename Book>
void run_benchmark_impl(const std::string& impl_name) {
    constexpr int NUM_OPERATIONS = 1000000;
    const int runs = 5;
    std::vector<double> total_times_ms;

    std::mt19937 seed_rng(42);

    for (int r = 0; r < runs; ++r) {
        Book book;
        std::mt19937 rng(42 + r);
        std::uniform_int_distribution<int> op_dist(0, 99);
        std::uniform_int_distribution<int> side_dist(0, 1);
        std::uniform_int_distribution<long> qty_dist(1, 100);
        std::uniform_int_distribution<int> bid_price_dist(5000, 9999);
        std::uniform_int_distribution<int> ask_price_dist(10001, 15000);

        std::vector<uint64_t> live_order_ids;
        live_order_ids.reserve(NUM_OPERATIONS);
        uint64_t next_id = 1;

        auto start = std::chrono::steady_clock::now();

        for (int i = 0; i < NUM_OPERATIONS; ++i) {
            int op = op_dist(rng);

            if (op < 70 || live_order_ids.empty()) {
                uint64_t id = next_id++;
                Side side = (side_dist(rng) == 0) ? Side::Buy : Side::Sell;
                double price = (side == Side::Buy) ? (bid_price_dist(rng) * 0.01) : (ask_price_dist(rng) * 0.01);
                long qty = qty_dist(rng);

                book.add_order(id, side, price, qty);
                live_order_ids.push_back(id);
            } else if (op < 90) {
                std::uniform_int_distribution<size_t> idx_dist(0, live_order_ids.size() - 1);
                size_t idx = idx_dist(rng);
                uint64_t id = live_order_ids[idx];

                live_order_ids[idx] = live_order_ids.back();
                live_order_ids.pop_back();

                book.cancel_order(id);
            } else {
                volatile auto bbo = book.get_bbo();
                (void)bbo;
            }

            if (i % 100 == 0) {
                volatile auto bids = book.get_bids(10);
                volatile auto asks = book.get_asks(10);
                (void)bids;
                (void)asks;
            }
        }

        auto end = std::chrono::steady_clock::now();
        std::chrono::duration<double, std::milli> elapsed = end - start;
        total_times_ms.push_back(elapsed.count());
    }

    std::sort(total_times_ms.begin(), total_times_ms.end());
    double median_time = total_times_ms[runs / 2];

    Book final_book;
    BBO final_bbo = final_book.get_bbo();
    long checksum = static_cast<long>(median_time * 1000.0);

    std::cout << "Implementation " << impl_name << " Benchmark Results:\n";
    std::cout << "  Total Time (Median of 5 runs): " << median_time << " ms\n";
    std::cout << "  Final BBO -> Bid: " << (final_bbo.has_bid ? std::to_string(final_bbo.bid_price) : "None")
              << " | Ask: " << (final_bbo.has_ask ? std::to_string(final_bbo.ask_price) : "None") << "\n";
    std::cout << "  Checksum: " << checksum << "\n";
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: ./bench [--verify | --impl A | --impl B | --impl C]\n";
        return 1;
    }

    std::string arg = argv[1];

    if (arg == "--verify") {
        run_correctness_test();
    } else if (arg == "--impl") {
        if (argc < 3) {
            std::cerr << "Please specify implementation A, B, or C.\n";
            return 1;
        }
        std::string impl = argv[2];
        if (impl == "A") {
            run_benchmark_impl<OrderBookA>("A");
        } else if (impl == "B") {
            run_benchmark_impl<OrderBookB>("B");
        } else if (impl == "C") {
            run_benchmark_impl<OrderBookC>("C");
        } else {
            std::cerr << "Unknown implementation: " << impl << " (expected A, B, or C)\n";
            return 1;
        }
    } else {
        std::cerr << "Unknown argument: " << arg << "\n";
        return 1;
    }

    return 0;
}
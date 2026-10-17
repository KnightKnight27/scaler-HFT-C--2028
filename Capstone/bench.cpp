#include "utils.hpp"
#include "orderbook_a.hpp"
#include "orderbook_b.hpp"
#include "orderbook_c.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <random>
#include <string>
#include <vector>

// Global checksum sink to prevent compiler dead-code elimination
volatile int64_t g_checksum = 0;

std::vector<Operation> generate_workload(size_t num_ops, uint32_t seed = 42) {
    std::mt19937 rng(seed);
    std::uniform_int_distribution<int> op_dist(1, 100);
    std::uniform_int_distribution<int> side_dist(0, 1);
    std::uniform_int_distribution<long> qty_dist(1, 100);
    std::uniform_int_distribution<int32_t> bid_tick_dist(MIN_BID_TICK, MAX_BID_TICK);
    std::uniform_int_distribution<int32_t> ask_tick_dist(MIN_ASK_TICK, MAX_ASK_TICK);

    std::vector<Operation> workload;
    workload.reserve(num_ops);

    std::vector<uint64_t> live_orders;
    live_orders.reserve(num_ops);

    uint64_t next_order_id = 1;

    for (size_t i = 0; i < num_ops; ++i) {
        int roll = op_dist(rng);

        // 70% Add, 20% Cancel (fallback to Add if no live orders), 10% BBO query
        if (roll <= 70 || (roll <= 90 && live_orders.empty())) {
            Side side = (side_dist(rng) == 0) ? Side::Buy : Side::Sell;
            int32_t tick = (side == Side::Buy) ? bid_tick_dist(rng) : ask_tick_dist(rng);
            double price = tick_to_price(tick);
            long qty = qty_dist(rng);
            uint64_t id = next_order_id++;

            live_orders.push_back(id);
            workload.push_back({OpType::Add, id, side, price, qty});
        } else if (roll <= 90) {
            std::uniform_int_distribution<size_t> cancel_idx_dist(0, live_orders.size() - 1);
            size_t idx = cancel_idx_dist(rng);
            uint64_t cancel_id = live_orders[idx];

            // Swap-and-pop in O(1) so we only ever cancel currently live orders
            live_orders[idx] = live_orders.back();
            live_orders.pop_back();

            workload.push_back({OpType::Cancel, cancel_id, Side::Buy, 0.0, 0});
        } else {
            workload.push_back({OpType::GetBBO, 0, Side::Buy, 0.0, 0});
        }
    }

    return workload;
}

bool same_price(double a, double b) {
    return price_to_tick(a) == price_to_tick(b);
}

bool bbo_matches(const BBO& x, const BBO& y) {
    if (x.has_bid != y.has_bid || x.has_ask != y.has_ask) return false;
    if (x.has_bid && (!same_price(x.bid_price, y.bid_price) || x.bid_qty != y.bid_qty)) return false;
    if (x.has_ask && (!same_price(x.ask_price, y.ask_price) || x.ask_qty != y.ask_qty)) return false;
    return true;
}

bool levels_match(const std::vector<Level>& x, const std::vector<Level>& y) {
    if (x.size() != y.size()) return false;
    for (size_t i = 0; i < x.size(); ++i) {
        if (!same_price(x[i].price, y[i].price) || x[i].qty != y[i].qty) {
            return false;
        }
    }
    return true;
}

int run_verification() {
    std::cout << "Running correctness test...\n\n";

    OrderBookA bookA;
    OrderBookB bookB;
    OrderBookC bookC(100'000);

    // 1. Edge Case Check: Empty Book & Unknown Cancel
    BBO emptyA = bookA.get_bbo();
    BBO emptyB = bookB.get_bbo();
    BBO emptyC = bookC.get_bbo();
    if (emptyA.has_bid || emptyA.has_ask || !bbo_matches(emptyA, emptyB) || !bbo_matches(emptyA, emptyC)) {
        std::cerr << "Failed empty book BBO check!\n";
        return 1;
    }
    if (bookA.cancel_order(999999) || bookB.cancel_order(999999) || bookC.cancel_order(999999)) {
        std::cerr << "Failed cancel unknown order check!\n";
        return 1;
    }

    // 2. Verify identical results on a random workload
    const size_t VERIFY_OPS = 50'000;
    auto workload = generate_workload(VERIFY_OPS, 42);

    for (size_t i = 0; i < workload.size(); ++i) {
        const auto& op = workload[i];

        if (op.type == OpType::Add) {
            bookA.add_order(op.id, op.side, op.price, op.qty);
            bookB.add_order(op.id, op.side, op.price, op.qty);
            bookC.add_order(op.id, op.side, op.price, op.qty);
        } else if (op.type == OpType::Cancel) {
            bool resA = bookA.cancel_order(op.id);
            bool resB = bookB.cancel_order(op.id);
            bool resC = bookC.cancel_order(op.id);
            if (resA != resB || resA != resC || !resA) {
                std::cerr << "Cancel mismatch at op " << i << "\n";
                return 1;
            }
            // Verify double-cancel returns false
            if (bookA.cancel_order(op.id) || bookB.cancel_order(op.id) || bookC.cancel_order(op.id)) {
                std::cerr << "Double-cancel failed to return false at op " << i << "\n";
                return 1;
            }
        }

        BBO bboA = bookA.get_bbo();
        BBO bboB = bookB.get_bbo();
        BBO bboC = bookC.get_bbo();
        if (!bbo_matches(bboA, bboB) || !bbo_matches(bboA, bboC)) {
            std::cerr << "BBO mismatch at op " << i << "\n";
            return 1;
        }

        // Check full depth ladder every 50 operations during verification
        if ((i + 1) % 50 == 0) {
            auto bidsA = bookA.get_bids(10);
            auto bidsB = bookB.get_bids(10);
            auto bidsC = bookC.get_bids(10);
            if (!levels_match(bidsA, bidsB) || !levels_match(bidsA, bidsC)) {
                std::cerr << "get_bids(10) mismatch at op " << i << "\n";
                return 1;
            }

            auto asksA = bookA.get_asks(10);
            auto asksB = bookB.get_asks(10);
            auto asksC = bookC.get_asks(10);
            if (!levels_match(asksA, asksB) || !levels_match(asksA, asksC)) {
                std::cerr << "get_asks(10) mismatch at op " << i << "\n";
                return 1;
            }
        }
    }

    std::cout << "Implementation A: PASS\n";
    std::cout << "Implementation B: PASS\n";
    std::cout << "Implementation C: PASS\n\n";
    std::cout << "All implementations produce identical results.\n";
    return 0;
}

struct RunMetrics {
    double total_ms;
    double ns_per_add;
    double ns_per_cancel;
    double ns_per_bbo;
    BBO final_bbo;
    int64_t checksum;
};

template <typename OrderBookImpl>
RunMetrics execute_single_run(const std::vector<Operation>& workload) {
    OrderBookImpl book;
    using Clock = std::chrono::steady_clock;

    uint64_t add_ns = 0, cancel_ns = 0, bbo_ns = 0;
    size_t add_count = 0, cancel_count = 0, bbo_count = 0;
    int64_t local_checksum = 0;

    auto total_start = Clock::now();

    for (size_t i = 0; i < workload.size(); ++i) {
        const auto& op = workload[i];

        if (op.type == OpType::Add) {
            auto t0 = Clock::now();
            book.add_order(op.id, op.side, op.price, op.qty);
            auto t1 = Clock::now();
            add_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
            ++add_count;
        } else if (op.type == OpType::Cancel) {
            auto t0 = Clock::now();
            bool ok = book.cancel_order(op.id);
            auto t1 = Clock::now();
            cancel_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
            ++cancel_count;
            local_checksum += ok ? 1 : 0;
        } else {
            auto t0 = Clock::now();
            BBO bbo = book.get_bbo();
            auto t1 = Clock::now();
            bbo_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
            ++bbo_count;
            if (bbo.has_bid) local_checksum += bbo.bid_qty;
            if (bbo.has_ask) local_checksum += bbo.ask_qty;
        }

        // Every 100 operations, call get_bids(10) and get_asks(10)
        if ((i + 1) % 100 == 0) {
            auto bids = book.get_bids(10);
            auto asks = book.get_asks(10);
            if (!bids.empty()) local_checksum += bids[0].qty;
            if (!asks.empty()) local_checksum += asks[0].qty;
        }
    }

    auto total_end = Clock::now();
    double total_ms = std::chrono::duration<double, std::milli>(total_end - total_start).count();

    g_checksum = local_checksum;

    return {
        total_ms,
        add_count ? static_cast<double>(add_ns) / add_count : 0.0,
        cancel_count ? static_cast<double>(cancel_ns) / cancel_count : 0.0,
        bbo_count ? static_cast<double>(bbo_ns) / bbo_count : 0.0,
        book.get_bbo(),
        local_checksum
    };
}

template <typename OrderBookImpl>
void run_benchmark_suite(const std::string& name, const std::vector<Operation>& workload) {
    constexpr int NUM_RUNS = 5;
    std::vector<double> total_times, add_times, cancel_times, bbo_times;
    RunMetrics last_run{};

    for (int r = 0; r < NUM_RUNS; ++r) {
        last_run = execute_single_run<OrderBookImpl>(workload);
        total_times.push_back(last_run.total_ms);
        add_times.push_back(last_run.ns_per_add);
        cancel_times.push_back(last_run.ns_per_cancel);
        bbo_times.push_back(last_run.ns_per_bbo);
    }

    auto median = [](std::vector<double>& v) {
        std::sort(v.begin(), v.end());
        return v[v.size() / 2];
    };

    std::cout << "=== Benchmark Results (Median of 5 runs): Implementation " << name << " ===\n";
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "Total execution time : " << median(total_times) << " ms\n";
    std::cout << "Average ns / add     : " << median(add_times) << " ns\n";
    std::cout << "Average ns / cancel  : " << median(cancel_times) << " ns\n";
    std::cout << "Average ns / get_bbo : " << median(bbo_times) << " ns\n\n";

    std::cout << "Final BBO:\n";
    if (last_run.final_bbo.has_bid) {
        std::cout << "Bid: " << last_run.final_bbo.bid_price << " x " << last_run.final_bbo.bid_qty << "\n";
    } else {
        std::cout << "Bid: NONE\n";
    }
    if (last_run.final_bbo.has_ask) {
        std::cout << "Ask: " << last_run.final_bbo.ask_price << " x " << last_run.final_bbo.ask_qty << "\n\n";
    } else {
        std::cout << "Ask: NONE\n\n";
    }

    std::cout << "Checksum: " << last_run.checksum << "\n";
}

int main(int argc, char* argv[]) {
    if (argc == 2 && std::string(argv[1]) == "--verify") {
        return run_verification();
    }

    if (argc == 3 && std::string(argv[1]) == "--impl") {
        std::string impl = argv[2];
        constexpr size_t WORKLOAD_OPS = 1'000'000;
        auto workload = generate_workload(WORKLOAD_OPS, 42);

        if (impl == "A") {
            run_benchmark_suite<OrderBookA>("A", workload);
        } else if (impl == "B") {
            run_benchmark_suite<OrderBookB>("B", workload);
        } else if (impl == "C") {
            run_benchmark_suite<OrderBookC>("C", workload);
        } else {
            std::cerr << "Unknown implementation: " << impl << " (Use A, B, or C)\n";
            return 1;
        }
        return 0;
    }

    std::cout << "Usage:\n"
              << "  ./bench --verify\n"
              << "  ./bench --impl A\n"
              << "  ./bench --impl B\n"
              << "  ./bench --impl C\n";
    return 1;
}
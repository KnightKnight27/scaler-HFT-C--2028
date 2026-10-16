
#include "order_book.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <random>
#include <string>
#include <unordered_set>
#include <vector>

using Clock = std::chrono::steady_clock;

constexpr int OPERATIONS = 1'000'000;
constexpr int RUNS = 5;

struct Timings {
    double total_ms = 0.0;
    double add_ns = 0.0;
    double cancel_ns = 0.0;
    double bbo_ns = 0.0;
    std::uint64_t checksum = 0;
    std::size_t add_count = 0;
    std::size_t cancel_count = 0;
    std::size_t bbo_count = 0;
};

struct WorkloadOperation {
    int type;  
    Side side = Side::Buy;
    int price_ticks = 0;
    long qty = 0;
    std::uint64_t order_id = 0;
};

std::vector<WorkloadOperation> make_workload() {
    std::mt19937 rng(123456);
    std::uniform_int_distribution<int> action_dist(0, 99);
    std::uniform_int_distribution<int> side_dist(0, 1);
    std::uniform_int_distribution<int> bid_dist(5000, 9999);
    std::uniform_int_distribution<int> ask_dist(10001, 15000);
    std::uniform_int_distribution<int> qty_dist(1, 100);

    std::vector<WorkloadOperation> workload;
    workload.reserve(OPERATIONS);

    
    for (int i = 0; i < OPERATIONS; ++i) {
        int action = action_dist(rng);

        WorkloadOperation op{};
        if (action < 70) {
            op.type = 0;
            op.side = side_dist(rng) == 0 ? Side::Buy : Side::Sell;
            op.price_ticks = op.side == Side::Buy
                ? bid_dist(rng) : ask_dist(rng);
            op.qty = qty_dist(rng);
        } else if (action < 90) {
            op.type = 1;
        } else {
            op.type = 2;
        }

        workload.push_back(op);
    }

    return workload;
}

std::unique_ptr<OrderBook> create_book(const std::string& impl) {
    if (impl == "A") return make_map_order_book();
    if (impl == "B") return make_heap_order_book();
    if (impl == "C") return make_array_order_book();
    return nullptr;
}

Timings run_once(
    const std::string& impl,
    const std::vector<WorkloadOperation>& workload
) {
    auto book = create_book(impl);

    
    std::mt19937 cancel_rng(98765);
    std::vector<std::uint64_t> live_ids;
    live_ids.reserve(OPERATIONS);

    std::uint64_t next_id = 1;
    std::uint64_t checksum = 0;

    std::size_t add_count = 0;
    std::size_t cancel_count = 0;
    std::size_t bbo_count = 0;

    double add_ns = 0.0;
    double cancel_ns = 0.0;
    double bbo_ns = 0.0;

    auto total_start = Clock::now();

    for (std::size_t i = 0; i < workload.size(); ++i) {
        const auto& op = workload[i];

        if (op.type == 0) {
            double price = op.price_ticks / 100.0;

            auto start = Clock::now();
            book->add_order(next_id, op.side, price, op.qty);
            auto end = Clock::now();

            add_ns += std::chrono::duration<double, std::nano>(
                end - start
            ).count();

            live_ids.push_back(next_id);
            checksum += next_id + static_cast<std::uint64_t>(op.price_ticks);
            ++next_id;
            ++add_count;
        
        } else if (op.type == 1) {
            if (!live_ids.empty()) {
                std::uniform_int_distribution<std::size_t> pick(
                    0, live_ids.size() - 1
                );

                std::size_t index = pick(cancel_rng);
                std::uint64_t id = live_ids[index];

                auto start = Clock::now();
                bool cancelled = book->cancel_order(id);
                auto end = Clock::now();

                cancel_ns += std::chrono::duration<double, std::nano>(
                    end - start
                ).count();

                
                ++cancel_count;

                
                if (!cancelled) {
                    std::cerr << "ERROR: failed to cancel live order ID "
                              << id << '\n';
                    std::exit(EXIT_FAILURE);
                }

                checksum += id;

                
                live_ids[index] = live_ids.back();
                live_ids.pop_back();
            }
        }else {
            auto start = Clock::now();
            BBO bbo = book->get_bbo();
            auto end = Clock::now();

            bbo_ns += std::chrono::duration<double, std::nano>(
                end - start
            ).count();

            ++bbo_count;

            if (bbo.has_bid) {
                checksum += static_cast<std::uint64_t>(bbo.bid_price * 100);
                checksum += static_cast<std::uint64_t>(bbo.bid_qty);
            }
            if (bbo.has_ask) {
                checksum += static_cast<std::uint64_t>(bbo.ask_price * 100);
                checksum += static_cast<std::uint64_t>(bbo.ask_qty);
            }
        }

        // Required depth queries every 100 operations.
        if ((i + 1) % 100 == 0) {
            auto bids = book->get_bids(10);
            auto asks = book->get_asks(10);

            for (const auto& level : bids) {
                checksum += static_cast<std::uint64_t>(level.price * 100);
                checksum += static_cast<std::uint64_t>(level.qty);
            }
            for (const auto& level : asks) {
                checksum += static_cast<std::uint64_t>(level.price * 100);
                checksum += static_cast<std::uint64_t>(level.qty);
            }
        }
    }

    auto total_end = Clock::now();

    Timings result;
    result.total_ms = std::chrono::duration<double, std::milli>(
        total_end - total_start
    ).count();
    result.add_ns = add_ns;
    result.cancel_ns = cancel_ns;
    result.bbo_ns = bbo_ns;
    result.checksum = checksum;
    result.add_count = add_count;
    result.cancel_count = cancel_count;
    result.bbo_count = bbo_count;
    return result;
}

double median(std::vector<double> values) {
    std::sort(values.begin(), values.end());
    return values[values.size() / 2];
}

int main(int argc, char* argv[]) {
    if (argc != 3 || std::string(argv[1]) != "--impl") {
        std::cerr << "Usage: " << argv[0] << " --impl A|B|C\n";
        return 1;
    }

    std::string impl = argv[2];
    if (impl != "A" && impl != "B" && impl != "C") {
        std::cerr << "Implementation must be A, B, or C.\n";
        return 1;
    }

    const auto workload = make_workload();

    std::vector<double> totals;
    std::vector<double> adds;
    std::vector<double> cancels;
    std::vector<double> bbo_times;
    std::uint64_t final_checksum = 0;
    std::size_t add_count = 0;
    std::size_t cancel_count = 0;
    std::size_t bbo_count = 0;

    for (int run = 0; run < RUNS; ++run) {
        Timings t = run_once(impl, workload);

        totals.push_back(t.total_ms);
        adds.push_back(t.add_ns);
        cancels.push_back(t.cancel_ns);
        bbo_times.push_back(t.bbo_ns);
        final_checksum = t.checksum;
        add_count = t.add_count;
        cancel_count = t.cancel_count;
        bbo_count = t.bbo_count;

        std::cout << "Run " << run + 1
                  << ": total = " << t.total_ms << " ms"
                  << ", checksum = " << t.checksum << '\n';
    }

    std::cout << "\nImplementation " << impl
              << " — median of " << RUNS << " runs\n";
    std::cout << "Total time: " << median(totals) << " ms\n";
    
std::cout << "Average add latency: "
<< (add_count ? median(adds) / add_count : 0.0)
<< " ns\n";

std::cout << "Average cancel latency: "
<< (cancel_count ? median(cancels) / cancel_count : 0.0)
<< " ns\n";

std::cout << "Average BBO latency: "
<< (bbo_count ? median(bbo_times) / bbo_count : 0.0)
<< " ns\n";

    std::cout << "Checksum: " << final_checksum << '\n';

    return 0;
}

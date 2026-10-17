#include "fixed_array_order_book.hpp"
#include "order_book.hpp"
#include "ordered_map_order_book.hpp"
#include "sorted_vector_order_book.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <exception>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <random>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

using namespace orderbook;
using Clock = std::chrono::steady_clock;
constexpr std::size_t kOperationCount = 1'000'000;
constexpr std::uint32_t kSeed = 42;
constexpr int kRepetitions = 5;

enum class OperationType { Add, Cancel, Bbo };

struct Operation {
    OperationType type;
    std::uint64_t id;
    Side side;
    PriceTicks price_ticks;
    long qty;
};

using Workload = std::vector<Operation>;

Workload make_workload() {
    std::mt19937 rng(kSeed);
    Workload workload;
    workload.reserve(kOperationCount);
    std::vector<std::uint64_t> live_ids;
    live_ids.reserve(kOperationCount);
    std::uint64_t next_id = 1;

    for (std::size_t i = 0; i < kOperationCount; ++i) {
        const auto category = rng() % 10;
        if (category < 7 || live_ids.empty()) {
            const auto id = next_id++;
            const auto side = (rng() & 1U) == 0 ? Side::Buy : Side::Sell;
            const auto price_ticks =
                side == Side::Buy
                    ? 5000 + static_cast<PriceTicks>(rng() % 5000)
                    : 10001 + static_cast<PriceTicks>(rng() % 5000);
            const auto qty = 1 + static_cast<long>(rng() % 100);
            workload.push_back({OperationType::Add, id, side, price_ticks, qty});
            live_ids.push_back(id);
        } else if (category < 9) {
            const auto position = static_cast<std::size_t>(rng() % live_ids.size());
            const auto id = live_ids[position];
            workload.push_back({OperationType::Cancel, id, Side::Buy, 0, 0});
            live_ids[position] = live_ids.back();
            live_ids.pop_back();
        } else {
            workload.push_back({OperationType::Bbo, 0, Side::Buy, 0, 0});
        }
    }
    return workload;
}

bool same_bbo(const BBO& lhs, const BBO& rhs) {
    return lhs.has_bid == rhs.has_bid && lhs.bid_price_ticks == rhs.bid_price_ticks &&
           lhs.bid_qty == rhs.bid_qty && lhs.has_ask == rhs.has_ask &&
           lhs.ask_price_ticks == rhs.ask_price_ticks && lhs.ask_qty == rhs.ask_qty;
}

bool same_levels(const std::vector<Level>& lhs, const std::vector<Level>& rhs) {
    if (lhs.size() != rhs.size()) {
        return false;
    }
    for (std::size_t i = 0; i < lhs.size(); ++i) {
        if (lhs[i].price_ticks != rhs[i].price_ticks || lhs[i].qty != rhs[i].qty) {
            return false;
        }
    }
    return true;
}

void check_empty_book_and_cancellation() {
    if (price_to_ticks(99.99) != 9999 ||
        std::abs(ticks_to_price(10001) - 100.01) > 1e-10) {
        throw std::runtime_error("price/tick conversion is incorrect");
    }

    OrderedMapOrderBook a;
    SortedVectorOrderBook b;
    FixedArrayOrderBook c;
    const auto empty_a = a.get_bbo();
    const auto empty_b = b.get_bbo();
    const auto empty_c = c.get_bbo();
    if (empty_a.has_bid || empty_a.has_ask || empty_b.has_bid || empty_b.has_ask ||
        empty_c.has_bid || empty_c.has_ask) {
        throw std::runtime_error("empty book reported a quote");
    }
    if (a.cancel_order(9) || b.cancel_order(9) || c.cancel_order(9)) {
        throw std::runtime_error("unknown order cancellation succeeded");
    }

    a.add_order(1, Side::Buy, 9999, 10);
    b.add_order(1, Side::Buy, 9999, 10);
    c.add_order(1, Side::Buy, 9999, 10);
    if (!a.cancel_order(1) || !b.cancel_order(1) || !c.cancel_order(1) ||
        a.cancel_order(1) || b.cancel_order(1) || c.cancel_order(1)) {
        throw std::runtime_error("live/already-cancelled order behavior is incorrect");
    }
    bool a_rejected_reuse = false;
    bool b_rejected_reuse = false;
    bool c_rejected_reuse = false;
    try {
        a.add_order(1, Side::Buy, 9999, 1);
    } catch (const std::invalid_argument&) {
        a_rejected_reuse = true;
    }
    try {
        b.add_order(1, Side::Buy, 9999, 1);
    } catch (const std::invalid_argument&) {
        b_rejected_reuse = true;
    }
    try {
        c.add_order(1, Side::Buy, 9999, 1);
    } catch (const std::invalid_argument&) {
        c_rejected_reuse = true;
    }
    if (!a_rejected_reuse || !b_rejected_reuse || !c_rejected_reuse) {
        throw std::runtime_error("a cancelled order ID was reused");
    }

    a.add_order(2, Side::Buy, 9999, 1);
    a.add_order(3, Side::Buy, 9998, 2);
    a.add_order(4, Side::Buy, 9997, 3);
    b.add_order(2, Side::Buy, 9999, 1);
    b.add_order(3, Side::Buy, 9998, 2);
    b.add_order(4, Side::Buy, 9997, 3);
    c.add_order(2, Side::Buy, 9999, 1);
    c.add_order(3, Side::Buy, 9998, 2);
    c.add_order(4, Side::Buy, 9997, 3);
    const auto three_a = a.get_bids(10);
    const auto three_b = b.get_bids(10);
    const auto three_c = c.get_bids(10);
    if (three_a.size() != 3 || !same_levels(three_a, three_b) ||
        !same_levels(three_a, three_c)) {
        throw std::runtime_error("insufficient-depth behavior is incorrect");
    }

    bool rejected_bad_price = false;
    try {
        a.add_order(5, Side::Buy, 10000, 1);
    } catch (const std::invalid_argument&) {
        rejected_bad_price = true;
    }
    if (!rejected_bad_price) {
        throw std::runtime_error("mid-price order should have been rejected");
    }

    bool rejected_fractional_tick = false;
    try {
        (void)price_to_ticks(99.991);
    } catch (const std::invalid_argument&) {
        rejected_fractional_tick = true;
    }
    if (!rejected_fractional_tick) {
        throw std::runtime_error("fractional-tick price should have been rejected");
    }
}

void verify_workload(const Workload& workload) {
    check_empty_book_and_cancellation();
    OrderedMapOrderBook a;
    SortedVectorOrderBook b;
    FixedArrayOrderBook c;

    std::cout << "Running correctness test on " << workload.size()
              << " operations...\n";
    for (std::size_t i = 0; i < workload.size(); ++i) {
        const auto& op = workload[i];
        bool a_cancelled = true;
        bool b_cancelled = true;
        bool c_cancelled = true;
        if (op.type == OperationType::Add) {
            a.add_order(op.id, op.side, op.price_ticks, op.qty);
            b.add_order(op.id, op.side, op.price_ticks, op.qty);
            c.add_order(op.id, op.side, op.price_ticks, op.qty);
        } else if (op.type == OperationType::Cancel) {
            a_cancelled = a.cancel_order(op.id);
            b_cancelled = b.cancel_order(op.id);
            c_cancelled = c.cancel_order(op.id);
        } else {
            (void)a.get_bbo();
            (void)b.get_bbo();
            (void)c.get_bbo();
        }

        if (a_cancelled != b_cancelled || a_cancelled != c_cancelled ||
            !same_bbo(a.get_bbo(), b.get_bbo()) ||
            !same_bbo(a.get_bbo(), c.get_bbo()) ||
            !same_levels(a.get_bids(10), b.get_bids(10)) ||
            !same_levels(a.get_bids(10), c.get_bids(10)) ||
            !same_levels(a.get_asks(10), b.get_asks(10)) ||
            !same_levels(a.get_asks(10), c.get_asks(10))) {
            throw std::runtime_error("implementations differ after operation " +
                                     std::to_string(i));
        }
    }
    std::cout << "Implementation A: PASS\n"
              << "Implementation B: PASS\n"
              << "Implementation C: PASS\n"
              << "All implementations produce identical results.\n";
}

struct Measurement {
    double total_ms = 0;
    double add_ns = 0;
    double cancel_ns = 0;
    double bbo_ns = 0;
    std::uint64_t add_p99_ns = 0;
    std::uint64_t cancel_p99_ns = 0;
    std::uint64_t bbo_p99_ns = 0;
    BBO final_bbo;
    std::uint64_t checksum = 0;
};

std::uint64_t percentile(std::vector<std::uint64_t> samples, double fraction) {
    if (samples.empty()) {
        return 0;
    }
    std::sort(samples.begin(), samples.end());
    const auto index = static_cast<std::size_t>(
        std::ceil(fraction * static_cast<double>(samples.size()))) - 1;
    return samples[index];
}

std::uint64_t measure_timer_overhead() {
    std::vector<std::uint64_t> samples;
    samples.reserve(10001);
    for (int i = 0; i < 10001; ++i) {
        const auto start = Clock::now();
        const auto end = Clock::now();
        samples.push_back(static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count()));
    }
    return percentile(std::move(samples), 0.50);
}

template <typename Book>
Measurement run_once(const Workload& workload) {
    Book book;
    std::uint64_t checksum = 0;
    std::vector<std::uint64_t> add_samples;
    std::vector<std::uint64_t> cancel_samples;
    std::vector<std::uint64_t> bbo_samples;
    add_samples.reserve(workload.size());
    cancel_samples.reserve(workload.size());
    bbo_samples.reserve(workload.size());
    const auto total_start = Clock::now();

    for (std::size_t i = 0; i < workload.size(); ++i) {
        const auto& op = workload[i];
        const auto start = Clock::now();
        if (op.type == OperationType::Add) {
            book.add_order(op.id, op.side, op.price_ticks, op.qty);
            add_samples.push_back(static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start)
                    .count()));
        } else if (op.type == OperationType::Cancel) {
            if (!book.cancel_order(op.id)) {
                throw std::runtime_error("generated cancellation was not live");
            }
            cancel_samples.push_back(static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start)
                    .count()));
        } else {
            const auto bbo = book.get_bbo();
            bbo_samples.push_back(static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start)
                    .count()));
            checksum += static_cast<std::uint64_t>(bbo.bid_price_ticks + 1) *
                        static_cast<std::uint64_t>(bbo.bid_qty + 1);
            checksum += static_cast<std::uint64_t>(bbo.ask_price_ticks + 1) *
                        static_cast<std::uint64_t>(bbo.ask_qty + 1);
        }
        if ((i + 1) % 100 == 0) {
            const auto bids = book.get_bids(10);
            const auto asks = book.get_asks(10);
            for (const auto& level : bids) {
                checksum += static_cast<std::uint64_t>(level.price_ticks) *
                            static_cast<std::uint64_t>(level.qty);
            }
            for (const auto& level : asks) {
                checksum += static_cast<std::uint64_t>(level.price_ticks) *
                            static_cast<std::uint64_t>(level.qty);
            }
        }
    }
    const auto total_end = Clock::now();
    const auto final_bbo = book.get_bbo();
    checksum += static_cast<std::uint64_t>(final_bbo.bid_price_ticks + 1) *
                static_cast<std::uint64_t>(final_bbo.bid_qty + 1);
    checksum += static_cast<std::uint64_t>(final_bbo.ask_price_ticks + 1) *
                static_cast<std::uint64_t>(final_bbo.ask_qty + 1);

    Measurement result;
    result.total_ms =
        std::chrono::duration<double, std::milli>(total_end - total_start).count();
    result.add_ns = add_samples.empty()
                        ? 0
                        : static_cast<double>(
                              std::accumulate(add_samples.begin(), add_samples.end(),
                                              std::uint64_t{0})) /
                              add_samples.size();
    result.cancel_ns =
        cancel_samples.empty()
            ? 0
            : static_cast<double>(
                  std::accumulate(cancel_samples.begin(), cancel_samples.end(),
                                  std::uint64_t{0})) /
                  cancel_samples.size();
    result.bbo_ns = bbo_samples.empty()
                        ? 0
                        : static_cast<double>(
                              std::accumulate(bbo_samples.begin(), bbo_samples.end(),
                                              std::uint64_t{0})) /
                              bbo_samples.size();
    result.add_p99_ns = percentile(std::move(add_samples), 0.99);
    result.cancel_p99_ns = percentile(std::move(cancel_samples), 0.99);
    result.bbo_p99_ns = percentile(std::move(bbo_samples), 0.99);
    result.final_bbo = final_bbo;
    result.checksum = checksum;
    return result;
}

double median(std::vector<double> values) {
    std::sort(values.begin(), values.end());
    return values[values.size() / 2];
}

template <typename Book>
void benchmark(const Workload& workload, const std::string& label,
               const std::string& design) {
    const auto timer_overhead_ns = measure_timer_overhead();
    std::cout << "Implementation " << label << " (" << design << "), "
              << workload.size() << " ops, " << kRepetitions << " runs\n"
              << "empty timer interval median " << timer_overhead_ns
              << " ns (may round to zero at clock resolution; timed operation "
                 "samples include timer cost)\n\n";

    std::vector<Measurement> results;
    results.reserve(kRepetitions);
    for (int i = 0; i < kRepetitions; ++i) {
        results.push_back(run_once<Book>(workload));
    }

    std::vector<double> total_ms;
    std::vector<double> add_ns;
    std::vector<double> cancel_ns;
    std::vector<double> bbo_ns;
    std::vector<double> add_p99_ns;
    std::vector<double> cancel_p99_ns;
    std::vector<double> bbo_p99_ns;
    for (std::size_t i = 0; i < results.size(); ++i) {
        const auto& result = results[i];
        total_ms.push_back(result.total_ms);
        add_ns.push_back(result.add_ns);
        cancel_ns.push_back(result.cancel_ns);
        bbo_ns.push_back(result.bbo_ns);
        add_p99_ns.push_back(static_cast<double>(result.add_p99_ns));
        cancel_p99_ns.push_back(static_cast<double>(result.cancel_p99_ns));
        bbo_p99_ns.push_back(static_cast<double>(result.bbo_p99_ns));
        std::cout << std::fixed << std::setprecision(1) << "run " << i + 1
                  << " total " << result.total_ms << " ms | add " << result.add_ns
                  << " ns (p99 " << result.add_p99_ns << ") | cancel "
                  << result.cancel_ns << " ns (p99 " << result.cancel_p99_ns
                  << ") | bbo " << result.bbo_ns << " ns (p99 "
                  << result.bbo_p99_ns << ")\n";
    }
    std::cout << std::fixed << std::setprecision(1)
              << "\nMEDIAN total " << median(total_ms) << " ms | add "
              << median(add_ns) << " ns (p99 " << median(add_p99_ns)
              << ") | cancel " << median(cancel_ns) << " ns (p99 "
              << median(cancel_p99_ns) << ") | bbo " << median(bbo_ns)
              << " ns (p99 " << median(bbo_p99_ns) << ")\n\n"
              << "Final BBO:\n";
    if (results.back().final_bbo.has_bid) {
        std::cout << "Bid: " << std::fixed << std::setprecision(2)
                  << ticks_to_price(results.back().final_bbo.bid_price_ticks) << " x "
                  << results.back().final_bbo.bid_qty << '\n';
    } else {
        std::cout << "Bid: none\n";
    }
    if (results.back().final_bbo.has_ask) {
        std::cout << "Ask: " << std::fixed << std::setprecision(2)
                  << ticks_to_price(results.back().final_bbo.ask_price_ticks) << " x "
                  << results.back().final_bbo.ask_qty << '\n';
    } else {
        std::cout << "Ask: none\n";
    }
    std::cout << "Checksum: " << results.back().checksum << '\n';
    if (!std::all_of(results.begin(), results.end(), [&](const Measurement& result) {
            return result.checksum == results.front().checksum;
        })) {
        throw std::runtime_error("benchmark repetitions produced different checksums");
    }
}

template <typename Book>
std::uint64_t run_perf_workload(const Workload& workload) {
    Book book;
    std::uint64_t checksum = 0;
    for (const auto& operation : workload) {
        if (operation.type == OperationType::Add) {
            book.add_order(operation.id, operation.side, operation.price_ticks,
                           operation.qty);
        } else if (operation.type == OperationType::Cancel) {
            checksum += book.cancel_order(operation.id) ? 1 : 0;
        } else {
            const auto bbo = book.get_bbo();
            checksum += bbo.has_bid ? static_cast<std::uint64_t>(bbo.bid_qty) : 0;
            checksum += bbo.has_ask ? static_cast<std::uint64_t>(bbo.ask_qty) : 0;
        }
    }
    const auto final_bbo = book.get_bbo();
    if (final_bbo.has_bid) {
        checksum += static_cast<std::uint64_t>(final_bbo.bid_price_ticks);
    }
    if (final_bbo.has_ask) {
        checksum += static_cast<std::uint64_t>(final_bbo.ask_price_ticks);
    }
    return checksum;
}

void perf_baseline(const Workload& workload) {
    std::cout << "Implementation none (workload generation only), "
              << workload.size() << " ops\n";
}

template <typename Book>
void perf_benchmark(const Workload& workload, const std::string& label) {
    const auto checksum = run_perf_workload<Book>(workload);
    std::cout << "Implementation " << label << " (single untimed run), "
              << workload.size() << " ops\n"
              << "Checksum: " << checksum << '\n';
}

void print_help(const char* executable) {
    std::cout << "Usage:\n"
              << "  " << executable << " --verify\n"
              << "  " << executable << " --impl A|B|C\n"
              << "  " << executable << " --impl A|B|C|none --perf\n";
}

}  // namespace

int main(int argc, char** argv) {
    try {
        if (argc == 2 && std::string(argv[1]) == "--help") {
            print_help(argv[0]);
            return 0;
        }
        const auto workload = make_workload();
        if (argc == 2 && std::string(argv[1]) == "--verify") {
            verify_workload(workload);
            return 0;
        }
        if (argc == 4 && std::string(argv[1]) == "--impl" &&
            std::string(argv[3]) == "--perf") {
            const std::string implementation = argv[2];
            if (implementation == "none") {
                perf_baseline(workload);
            } else if (implementation == "A") {
                perf_benchmark<OrderedMapOrderBook>(workload, implementation);
            } else if (implementation == "B") {
                perf_benchmark<SortedVectorOrderBook>(workload, implementation);
            } else if (implementation == "C") {
                perf_benchmark<FixedArrayOrderBook>(workload, implementation);
            } else {
                print_help(argv[0]);
                return 2;
            }
            return 0;
        }
        if (argc != 3 || std::string(argv[1]) != "--impl") {
            print_help(argv[0]);
            return 2;
        }
        const std::string implementation = argv[2];
        if (implementation == "A") {
            benchmark<OrderedMapOrderBook>(workload, implementation, "ordered map");
        } else if (implementation == "B") {
            benchmark<SortedVectorOrderBook>(workload, implementation, "sorted vector");
        } else if (implementation == "C") {
            benchmark<FixedArrayOrderBook>(workload, implementation, "fixed tick array");
        } else {
            print_help(argv[0]);
            return 2;
        }
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << '\n';
        return 1;
    }
    return 0;
}

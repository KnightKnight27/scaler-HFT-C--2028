#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <random>
#include <string>
#include <vector>

// Include each implementation in its own type namespace using preprocessor aliases.
// This keeps one benchmark executable and one shared workload for A, B, and C.
#define Side SideA
#define Level LevelA
#define BBO BBOA
#define Order OrderA
#define OrderBook OrderBookA
#define main implementation_a_placeholder_main
#include "Impl1.cpp"
#undef main
#undef OrderBook
#undef Order
#undef BBO
#undef Level
#undef Side

#define Side SideB
#define Level LevelB
#define BBO BBOB
#define Order OrderB
#define OrderBook OrderBookB
#define main implementation_b_placeholder_main
#include "Impl2.cpp"
#undef main
#undef OrderBook
#undef Order
#undef BBO
#undef Level
#undef Side

#define Side SideC
#define Level LevelC
#define BBO BBOC
#define Order OrderC
#define OrderBook OrderBookC
#define main implementation_c_placeholder_main
#include "Impl3.cpp"
#undef main
#undef OrderBook
#undef Order
#undef BBO
#undef Level
#undef Side

using Clock = std::chrono::steady_clock;
static constexpr std::size_t OP_COUNT = 1'000'000;
static constexpr std::uint32_t SEED = 42;

enum class OpType { Add, Cancel, Bbo };

struct Operation {
    OpType type{};
    std::uint64_t id{};
    bool buy{};
    int price{};
    long qty{};
};

struct Metrics {
    double total_ms{};
    double add_ns{};
    double cancel_ns{};
    double bbo_ns{};
    std::uint64_t checksum{};
    std::size_t adds{};
    std::size_t cancels{};
    std::size_t bbo_queries{};
};

struct Snapshot {
    bool has_bid{};
    int bid_price{};
    long bid_qty{};
    bool has_ask{};
    int ask_price{};
    long ask_qty{};
    std::vector<std::pair<int, long>> bids;
    std::vector<std::pair<int, long>> asks;
};

std::vector<Operation> make_workload() {
    std::mt19937 rng(SEED);
    std::vector<Operation> workload;
    workload.reserve(OP_COUNT);
    std::vector<std::uint64_t> live_ids;
    live_ids.reserve(OP_COUNT);
    std::uint64_t next_id = 1;

    for (std::size_t i = 0; i < OP_COUNT; ++i) {
        const unsigned roll = rng() % 100;

        if (roll < 70 || live_ids.empty()) {
            const bool buy = (rng() % 2) == 0;
            const int price = buy
                ? 5000 + static_cast<int>(rng() % 5000)
                : 10001 + static_cast<int>(rng() % 5000);
            const long qty = 1 + static_cast<long>(rng() % 100);
            const std::uint64_t id = next_id++;
            workload.push_back({OpType::Add, id, buy, price, qty});
            live_ids.push_back(id);
        } else if (roll < 90) {
            const std::size_t chosen = rng() % live_ids.size();
            const std::uint64_t id = live_ids[chosen];
            workload.push_back({OpType::Cancel, id, false, 0, 0});
            live_ids[chosen] = live_ids.back();
            live_ids.pop_back();
        } else {
            workload.push_back({OpType::Bbo, 0, false, 0, 0});
        }
    }

    return workload;
}

template <typename Book, typename BboType>
Snapshot take_bbo_snapshot(const Book& book) {
    Snapshot s;
    const BboType bbo = book.get_bbo();
    s.has_bid = bbo.has_bid;
    s.bid_price = bbo.bid_price;
    s.bid_qty = bbo.bid_qty;
    s.has_ask = bbo.has_ask;
    s.ask_price = bbo.ask_price;
    s.ask_qty = bbo.ask_qty;
    return s;
}

template <typename Book, typename SideType, typename BboType, typename LevelType>
Snapshot take_snapshot(const Book& book) {
    Snapshot s = take_bbo_snapshot<Book, BboType>(book);
    for (const LevelType& level : book.get_bids(10))
        s.bids.emplace_back(level.price, level.qty);
    for (const LevelType& level : book.get_asks(10))
        s.asks.emplace_back(level.price, level.qty);
    return s;
}

bool same_snapshot(const Snapshot& a, const Snapshot& b) {
    return a.has_bid == b.has_bid &&
           a.bid_price == b.bid_price && a.bid_qty == b.bid_qty &&
           a.has_ask == b.has_ask &&
           a.ask_price == b.ask_price && a.ask_qty == b.ask_qty &&
           a.bids == b.bids && a.asks == b.asks;
}

template <typename Book, typename SideType, typename BboType, typename LevelType>
void apply_operation(Book& book, const Operation& op, std::uint64_t& checksum) {
    if (op.type == OpType::Add) {
        book.add_order(op.id, op.buy ? SideType::Buy : SideType::Sell, op.price, op.qty);
    } else if (op.type == OpType::Cancel) {
        const bool cancelled = book.cancel_order(op.id);
        checksum += cancelled ? 1 : 17;
    } else {
        const BboType bbo = book.get_bbo();
        checksum += static_cast<std::uint64_t>(bbo.has_bid) * 3;
        checksum += static_cast<std::uint64_t>(bbo.has_ask) * 5;
        checksum += static_cast<std::uint64_t>(bbo.bid_price) * 7;
        checksum += static_cast<std::uint64_t>(bbo.bid_qty) * 11;
        checksum += static_cast<std::uint64_t>(bbo.ask_price) * 13;
        checksum += static_cast<std::uint64_t>(bbo.ask_qty) * 17;
    }
}

template <typename Book, typename SideType, typename BboType, typename LevelType>
Metrics run_once(const std::vector<Operation>& workload) {
    Book book;
    Metrics metrics;
    std::uint64_t checksum = 0;
    const auto total_start = Clock::now();

    for (std::size_t i = 0; i < workload.size(); ++i) {
        const Operation& op = workload[i];

        if (op.type == OpType::Add) {
            const auto start = Clock::now();
            book.add_order(op.id, op.buy ? SideType::Buy : SideType::Sell, op.price, op.qty);
            const auto end = Clock::now();
            metrics.add_ns += std::chrono::duration<double, std::nano>(end - start).count();
            ++metrics.adds;
        } else if (op.type == OpType::Cancel) {
            const auto start = Clock::now();
            const bool cancelled = book.cancel_order(op.id);
            const auto end = Clock::now();
            metrics.cancel_ns += std::chrono::duration<double, std::nano>(end - start).count();
            checksum += cancelled ? 1 : 17;
            ++metrics.cancels;
        } else {
            const auto start = Clock::now();
            const BboType bbo = book.get_bbo();
            const auto end = Clock::now();
            metrics.bbo_ns += std::chrono::duration<double, std::nano>(end - start).count();
            checksum += static_cast<std::uint64_t>(bbo.has_bid) * 3;
            checksum += static_cast<std::uint64_t>(bbo.has_ask) * 5;
            checksum += static_cast<std::uint64_t>(bbo.bid_price) * 7;
            checksum += static_cast<std::uint64_t>(bbo.bid_qty) * 11;
            checksum += static_cast<std::uint64_t>(bbo.ask_price) * 13;
            checksum += static_cast<std::uint64_t>(bbo.ask_qty) * 17;
            ++metrics.bbo_queries;
        }

        // The assignment requires depth-10 reads every 100 operations.
        if ((i + 1) % 100 == 0) {
            const auto bid_levels = book.get_bids(10);
            const auto ask_levels = book.get_asks(10);
            if (!bid_levels.empty())
                checksum += static_cast<std::uint64_t>(bid_levels.front().price) * 19
                          + static_cast<std::uint64_t>(bid_levels.front().qty) * 23;
            if (!ask_levels.empty())
                checksum += static_cast<std::uint64_t>(ask_levels.front().price) * 29
                          + static_cast<std::uint64_t>(ask_levels.front().qty) * 31;
            checksum += bid_levels.size() + ask_levels.size();
        }
    }

    const auto total_end = Clock::now();
    metrics.total_ms = std::chrono::duration<double, std::milli>(total_end - total_start).count();
    const Snapshot final = take_snapshot<Book, SideType, BboType, LevelType>(book);
    checksum += static_cast<std::uint64_t>(final.bid_price) * 37
              + static_cast<std::uint64_t>(final.bid_qty) * 41
              + static_cast<std::uint64_t>(final.ask_price) * 43
              + static_cast<std::uint64_t>(final.ask_qty) * 47
              + final.bids.size() * 53 + final.asks.size() * 59;
    metrics.checksum = checksum;
    return metrics;
}

template <typename Book, typename SideType, typename BboType, typename LevelType>
bool edge_tests(const std::string& name) {
    Book book;
    const Snapshot empty = take_snapshot<Book, SideType, BboType, LevelType>(book);
    if (empty.has_bid || empty.has_ask || book.cancel_order(999999)) {
        std::cerr << name << ": empty-book / unknown-cancel test FAILED\n";
        return false;
    }

    book.add_order(1, SideType::Buy, 9900, 10);
    book.add_order(2, SideType::Buy, 9900, 7);
    book.add_order(3, SideType::Buy, 9800, 4);
    book.add_order(4, SideType::Sell, 10001, 8);
    book.add_order(5, SideType::Sell, 10100, 9);

    const auto bids = book.get_bids(10);
    const auto asks = book.get_asks(10);
    if (bids.size() != 2 || bids[0].price != 9900 || bids[0].qty != 17 ||
        bids[1].price != 9800 || asks.size() != 2 ||
        asks[0].price != 10001 || asks[0].qty != 8 ||
        asks[1].price != 10100) {
        std::cerr << name << ": aggregation / ordering / insufficient-depth test FAILED\n";
        return false;
    }
    if (!book.cancel_order(2) || book.cancel_order(2)) {
        std::cerr << name << ": cancel / repeated-cancel test FAILED\n";
        return false;
    }
    std::cout << name << ": edge tests PASS\n";
    return true;
}

bool verify_workload(const std::vector<Operation>& workload) {
    OrderBookA a;
    OrderBookB b;
    OrderBookC c;
    std::uint64_t checksum_a = 0, checksum_b = 0, checksum_c = 0;

    std::cout << "Running correctness verification on " << workload.size()
              << " shared operations...\n";

    for (std::size_t i = 0; i < workload.size(); ++i) {
        const Operation& op = workload[i];

        const bool ca = op.type == OpType::Cancel
            ? a.cancel_order(op.id) : false;
        const bool cb = op.type == OpType::Cancel
            ? b.cancel_order(op.id) : false;
        const bool cc = op.type == OpType::Cancel
            ? c.cancel_order(op.id) : false;

        if (op.type == OpType::Add) {
            a.add_order(op.id, op.buy ? SideA::Buy : SideA::Sell, op.price, op.qty);
            b.add_order(op.id, op.buy ? SideB::Buy : SideB::Sell, op.price, op.qty);
            c.add_order(op.id, op.buy ? SideC::Buy : SideC::Sell, op.price, op.qty);
        } else if (op.type == OpType::Bbo) {
            const auto ba = a.get_bbo();
            const auto bb = b.get_bbo();
            const auto bc = c.get_bbo();
            checksum_a += static_cast<std::uint64_t>(ba.bid_price + ba.ask_price + ba.bid_qty + ba.ask_qty);
            checksum_b += static_cast<std::uint64_t>(bb.bid_price + bb.ask_price + bb.bid_qty + bb.ask_qty);
            checksum_c += static_cast<std::uint64_t>(bc.bid_price + bc.ask_price + bc.bid_qty + bc.ask_qty);

            if (ba.has_bid != bb.has_bid || ba.bid_price != bb.bid_price ||
                ba.bid_qty != bb.bid_qty || ba.has_ask != bb.has_ask ||
                ba.ask_price != bb.ask_price || ba.ask_qty != bb.ask_qty ||
                ba.has_bid != bc.has_bid || ba.bid_price != bc.bid_price ||
                ba.bid_qty != bc.bid_qty || ba.has_ask != bc.has_ask ||
                ba.ask_price != bc.ask_price || ba.ask_qty != bc.ask_qty) {
                std::cerr << "BBO mismatch at operation " << i + 1 << '\n';
                return false;
            }
        } else if (ca != cb || ca != cc) {
            std::cerr << "Cancel return-value mismatch at operation " << i + 1 << '\n';
            return false;
        }

        // Compare full depth outputs every 100 operations, matching the workload.
        if ((i + 1) % 100 == 0) {
            const auto sa = take_snapshot<OrderBookA, SideA, BBOA, LevelA>(a);
            const auto sb = take_snapshot<OrderBookB, SideB, BBOB, LevelB>(b);
            const auto sc = take_snapshot<OrderBookC, SideC, BBOC, LevelC>(c);
            if (!same_snapshot(sa, sb) || !same_snapshot(sa, sc)) {
                std::cerr << "Depth mismatch at operation " << i + 1 << '\n';
                return false;
            }
        }
    }

    const auto final_a = take_snapshot<OrderBookA, SideA, BBOA, LevelA>(a);
    const auto final_b = take_snapshot<OrderBookB, SideB, BBOB, LevelB>(b);
    const auto final_c = take_snapshot<OrderBookC, SideC, BBOC, LevelC>(c);
    if (!same_snapshot(final_a, final_b) || !same_snapshot(final_a, final_c) ||
        checksum_a != checksum_b || checksum_a != checksum_c) {
        std::cerr << "Final state/checksum mismatch\n";
        return false;
    }

    std::cout << "Implementation A: PASS\n"
              << "Implementation B: PASS\n"
              << "Implementation C: PASS\n"
              << "All implementations agree on the shared workload.\n";
    return true;
}

template <typename Book, typename SideType, typename BboType, typename LevelType>
int benchmark_impl(const std::string& name,
                   const std::vector<Operation>& workload,
                   int runs) {
    std::vector<Metrics> results;
    results.reserve(runs);

    for (int i = 0; i < runs; ++i) {
        results.push_back(run_once<Book, SideType, BboType, LevelType>(workload));
        std::cout << name << " run " << (i + 1) << "/" << runs
                  << ": total_ms=" << std::fixed << std::setprecision(3)
                  << results.back().total_ms
                  << ", checksum=" << results.back().checksum << '\n';
    }

    auto median_value = [&](auto member) {
        std::vector<double> values;
        values.reserve(results.size());
        for (const auto& r : results) values.push_back(r.*member);
        std::sort(values.begin(), values.end());
        return values[values.size() / 2];
    };

    const Metrics& counts = results.front();
    const double total_ms = median_value(&Metrics::total_ms);
    const double add_ns = median_value(&Metrics::add_ns) / counts.adds;
    const double cancel_ns = median_value(&Metrics::cancel_ns) / counts.cancels;
    const double bbo_ns = median_value(&Metrics::bbo_ns) / counts.bbo_queries;

    std::cout << "\nMedian results — Implementation " << name << '\n'
              << "Operations: " << workload.size()
              << " (add=" << counts.adds << ", cancel=" << counts.cancels
              << ", BBO=" << counts.bbo_queries << ")\n"
              << "Total execution time (ms): " << total_ms << '\n'
              << "Average ns/add: " << add_ns << '\n'
              << "Average ns/cancel: " << cancel_ns << '\n'
              << "Average ns/BBO: " << bbo_ns << '\n'
              << "Final checksum: " << counts.checksum << "\n\n";
    return 0;
}

int main(int argc, char** argv) {
    std::string impl;
    int runs = 5;
    bool verify = false;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--impl" && i + 1 < argc) impl = argv[++i];
        else if (arg == "--runs" && i + 1 < argc) runs = std::max(1, std::atoi(argv[++i]));
        else if (arg == "--verify") verify = true;
        else if (arg == "--help") {
            std::cout << "Usage: ./bench --verify\n"
                      << "       ./bench --impl A|B|C [--runs N]\n";
            return 0;
        } else {
            std::cerr << "Unknown/incomplete argument: " << arg << '\n';
            return 2;
        }
    }

    const auto workload = make_workload();

    if (verify) {
        bool ok = edge_tests<OrderBookA, SideA, BBOA, LevelA>("Implementation A");
        ok = edge_tests<OrderBookB, SideB, BBOB, LevelB>("Implementation B") && ok;
        ok = edge_tests<OrderBookC, SideC, BBOC, LevelC>("Implementation C") && ok;
        if (!ok || !verify_workload(workload)) return 1;
        return 0;
    }

    if (impl == "A")
        return benchmark_impl<OrderBookA, SideA, BBOA, LevelA>("A", workload, runs);
    if (impl == "B")
        return benchmark_impl<OrderBookB, SideB, BBOB, LevelB>("B", workload, runs);
    if (impl == "C")
        return benchmark_impl<OrderBookC, SideC, BBOC, LevelC>("C", workload, runs);

    std::cerr << "Choose --verify or --impl A|B|C. Use --help for usage.\n";
    return 2;
}

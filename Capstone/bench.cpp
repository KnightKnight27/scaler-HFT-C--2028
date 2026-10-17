
#include "map_book.hpp"
#include "heap_book.hpp"
#include "array_book.hpp"
#include "segment_tree_book.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <random>
#include <string>
#include <vector>

using namespace orderbook;

namespace {

using Clock = std::chrono::steady_clock;

constexpr std::size_t OPERATION_COUNT = 1'000'000;
constexpr int RUN_COUNT = 5;
constexpr std::uint64_t RANDOM_SEED = 0xC0FFEE;

enum class Action {
    Add,
    Cancel,
    BBO
};

struct Operation {
    Action action;
    OrderId id = 0;
    Side side = Side::Buy;
    Price price = 0;
    Quantity qty = 0;
};

struct Metrics {
    std::int64_t total_ns = 0;
    std::int64_t add_ns = 0;
    std::int64_t cancel_ns = 0;
    std::int64_t bbo_ns = 0;

    std::uint64_t adds = 0;
    std::uint64_t cancels = 0;
    std::uint64_t bbo_queries = 0;
    std::uint64_t checksum = 0;
};

bool same_bbo(const BBO& a, const BBO& b) {
    return a.has_bid == b.has_bid &&
           a.bid_price == b.bid_price &&
           a.bid_qty == b.bid_qty &&
           a.has_ask == b.has_ask &&
           a.ask_price == b.ask_price &&
           a.ask_qty == b.ask_qty;
}

bool same_levels(
    const std::vector<Level>& a,
    const std::vector<Level>& b
) {
    if (a.size() != b.size()) {
        return false;
    }

    for (std::size_t i = 0; i < a.size(); ++i) {
        if (a[i].price != b[i].price ||
            a[i].qty != b[i].qty) {
            return false;
        }
    }

    return true;
}

void require(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "BENCHMARK VALIDATION FAILED: "
                  << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

std::vector<Operation> generate_workload() {
    std::mt19937_64 rng(RANDOM_SEED);

    std::vector<Operation> operations;
    operations.reserve(OPERATION_COUNT);

    std::vector<OrderId> live_ids;
    live_ids.reserve(OPERATION_COUNT);

    OrderId next_id = 1;

    for (std::size_t i = 0; i < OPERATION_COUNT; ++i) {
        const std::uint64_t roll = rng() % 100;

        if (roll < 70 || live_ids.empty()) {
            const Side side = (rng() % 2 == 0)
                ? Side::Buy
                : Side::Sell;

            const Price price = side == Side::Buy
                ? 5000 + static_cast<Price>(rng() % 5000)
                : 10001 + static_cast<Price>(rng() % 5000);

            const Quantity qty =
                1 + static_cast<Quantity>(rng() % 100);

            const OrderId id = next_id++;

            operations.push_back({
                Action::Add, id, side, price, qty
            });

            live_ids.push_back(id);

        } else if (roll < 90) {
            const std::size_t index =
                static_cast<std::size_t>(rng() % live_ids.size());

            const OrderId id = live_ids[index];

            operations.push_back({
                Action::Cancel, id, Side::Buy, 0, 0
            });

            live_ids[index] = live_ids.back();
            live_ids.pop_back();

        } else {
            operations.push_back({
                Action::BBO, 0, Side::Buy, 0, 0
            });
        }
    }

    return operations;
}

void verify_implementations(
    const std::vector<Operation>& operations
) {
    MapOrderBook a;
    HeapOrderBook b;
    ArrayOrderBook c;
    SegmentTreeOrderBook d;

    for (std::size_t i = 0; i < operations.size(); ++i) {
        const Operation& op = operations[i];

        bool ra = true;
        bool rb = true;
        bool rc = true;
        bool rd = true;

        switch (op.action) {
        case Action::Add:
            ra = a.add_order(op.id, op.side, op.price, op.qty);
            rb = b.add_order(op.id, op.side, op.price, op.qty);
            rc = c.add_order(op.id, op.side, op.price, op.qty);
            rd = d.add_order(op.id, op.side, op.price, op.qty);

            require(
                ra == rb && ra == rc && ra == rd,
                "Add result mismatch at operation " +
                std::to_string(i)
            );
            break;

        case Action::Cancel:
            ra = a.cancel_order(op.id);
            rb = b.cancel_order(op.id);
            rc = c.cancel_order(op.id);
            rd = d.cancel_order(op.id);

            require(
                ra == rb && ra == rc && ra == rd,
                "Cancel result mismatch at operation " +
                std::to_string(i)
            );
            break;

        case Action::BBO:
            break;
        }

        const BBO ba = a.get_bbo();
        const BBO bb = b.get_bbo();
        const BBO bc = c.get_bbo();
        const BBO bd = d.get_bbo();

        require(
            same_bbo(ba, bb),
            "A/B BBO mismatch at operation " + std::to_string(i)
        );

        require(
            same_bbo(ba, bc),
            "A/C BBO mismatch at operation " + std::to_string(i)
        );

        require(
            same_bbo(ba, bd),
            "A/D BBO mismatch at operation " + std::to_string(i)
        );

        if (i % 100 == 0) {
            const auto bids = a.get_bids(10);
            const auto asks = a.get_asks(10);

            require(
                same_levels(bids, b.get_bids(10)),
                "A/B bid depth mismatch at operation " +
                std::to_string(i)
            );

            require(
                same_levels(asks, b.get_asks(10)),
                "A/B ask depth mismatch at operation " +
                std::to_string(i)
            );

            require(
                same_levels(bids, c.get_bids(10)),
                "A/C bid depth mismatch at operation " +
                std::to_string(i)
            );

            require(
                same_levels(asks, c.get_asks(10)),
                "A/C ask depth mismatch at operation " +
                std::to_string(i)
            );

            require(
                same_levels(bids, d.get_bids(10)),
                "A/D bid depth mismatch at operation " +
                std::to_string(i)
            );

            require(
                same_levels(asks, d.get_asks(10)),
                "A/D ask depth mismatch at operation " +
                std::to_string(i)
            );
        }
    }

    std::cout
        << "Four-way correctness pre-check passed for "
        << operations.size() << " operations.\n";
}

std::uint64_t mix(
    std::uint64_t hash,
    std::uint64_t value
) {
    hash ^= value + 0x9e3779b97f4a7c15ULL +
            (hash << 6) + (hash >> 2);

    return hash;
}

void include_bbo_checksum(
    std::uint64_t& checksum,
    const BBO& bbo
) {
    checksum = mix(checksum, bbo.has_bid);
    checksum = mix(
        checksum,
        static_cast<std::uint64_t>(bbo.bid_price)
    );
    checksum = mix(
        checksum,
        static_cast<std::uint64_t>(bbo.bid_qty)
    );
    checksum = mix(checksum, bbo.has_ask);
    checksum = mix(
        checksum,
        static_cast<std::uint64_t>(bbo.ask_price)
    );
    checksum = mix(
        checksum,
        static_cast<std::uint64_t>(bbo.ask_qty)
    );
}

void include_depth_checksum(
    std::uint64_t& checksum,
    const std::vector<Level>& levels
) {
    checksum = mix(checksum, levels.size());

    for (const Level& level : levels) {
        checksum = mix(
            checksum,
            static_cast<std::uint64_t>(level.price)
        );

        checksum = mix(
            checksum,
            static_cast<std::uint64_t>(level.qty)
        );
    }
}

template <typename Book>
Metrics run_benchmark(
    Book& book,
    const std::vector<Operation>& operations
) {
    Metrics metrics;

    const auto total_start = Clock::now();

    for (std::size_t i = 0; i < operations.size(); ++i) {
        const Operation& op = operations[i];

        switch (op.action) {
        case Action::Add: {
            const auto start = Clock::now();

            const bool ok = book.add_order(
                op.id, op.side, op.price, op.qty
            );

            const auto end = Clock::now();

            require(ok, "Benchmark add unexpectedly failed");

            metrics.add_ns +=
                std::chrono::duration_cast<
                    std::chrono::nanoseconds
                >(end - start).count();

            ++metrics.adds;
            break;
        }

        case Action::Cancel: {
            const auto start = Clock::now();

            const bool ok = book.cancel_order(op.id);

            const auto end = Clock::now();

            require(ok, "Benchmark cancel unexpectedly failed");

            metrics.cancel_ns +=
                std::chrono::duration_cast<
                    std::chrono::nanoseconds
                >(end - start).count();

            ++metrics.cancels;
            break;
        }

        case Action::BBO: {
            const auto start = Clock::now();

            const BBO bbo = book.get_bbo();

            const auto end = Clock::now();

            metrics.bbo_ns +=
                std::chrono::duration_cast<
                    std::chrono::nanoseconds
                >(end - start).count();

            include_bbo_checksum(metrics.checksum, bbo);
            ++metrics.bbo_queries;
            break;
        }
        }
        if (i % 100 == 0) {
            include_depth_checksum(
                metrics.checksum, book.get_bids(10)
            );

            include_depth_checksum(
                metrics.checksum, book.get_asks(10)
            );
        }
    }

    const auto total_end = Clock::now();

    metrics.total_ns =
        std::chrono::duration_cast<
            std::chrono::nanoseconds
        >(total_end - total_start).count();

    return metrics;
}

std::int64_t median(std::vector<std::int64_t> values) {
    std::sort(values.begin(), values.end());
    return values[values.size() / 2];
}

void print_results(
    const std::string& name,
    const std::vector<Metrics>& runs
) {
    std::vector<std::int64_t> total_times;
    std::vector<std::int64_t> add_times;
    std::vector<std::int64_t> cancel_times;
    std::vector<std::int64_t> bbo_times;

    for (const Metrics& m : runs) {
        total_times.push_back(m.total_ns);

        add_times.push_back(
            m.adds == 0
                ? 0
                : m.add_ns / static_cast<std::int64_t>(m.adds)
        );

        cancel_times.push_back(
            m.cancels == 0
                ? 0
                : m.cancel_ns / static_cast<std::int64_t>(m.cancels)
        );

        bbo_times.push_back(
            m.bbo_queries == 0
                ? 0
                : m.bbo_ns /
                    static_cast<std::int64_t>(m.bbo_queries)
        );
    }

    std::cout << "\n=== " << name << " ===\n";

    std::cout << std::fixed << std::setprecision(3);
    std::cout << "Total median: "
              << median(total_times) / 1e6 << " ms\n";

    std::cout << "Median ns/add: "
              << median(add_times) << '\n';

    std::cout << "Median ns/cancel: "
              << median(cancel_times) << '\n';

    std::cout << "Median ns/BBO: "
              << median(bbo_times) << '\n';

    std::cout << "Per-run total time (ms): ";

    for (const Metrics& m : runs) {
        std::cout << m.total_ns / 1e6 << ' ';
    }

    std::cout << "\nChecksums: ";

    for (const Metrics& m : runs) {
        std::cout << m.checksum << ' ';
    }

    std::cout << '\n';
}

} // namespace

int main() {
    std::cout << "Order Book Benchmark\n";

    std::cout << "Operations: " << OPERATION_COUNT
              << ", runs per implementation: " << RUN_COUNT << '\n';

    std::cout << "Seed: " << RANDOM_SEED << '\n';

    const std::vector<Operation> workload = generate_workload();

    std::uint64_t add_count = 0;
    std::uint64_t cancel_count = 0;
    std::uint64_t bbo_count = 0;

    for (const Operation& op : workload) {
        if (op.action == Action::Add) {
            ++add_count;
        } else if (op.action == Action::Cancel) {
            ++cancel_count;
        } else {
            ++bbo_count;
        }
    }

    std::cout << "Workload counts: adds=" << add_count
              << ", cancels=" << cancel_count
              << ", BBO queries=" << bbo_count << '\n';

    verify_implementations(workload);

    std::vector<Metrics> map_runs;
    std::vector<Metrics> heap_runs;
    std::vector<Metrics> array_runs;
    std::vector<Metrics> segment_tree_runs;

    for (int run = 1; run <= RUN_COUNT; ++run) {
        MapOrderBook a;
        map_runs.push_back(run_benchmark(a, workload));

        HeapOrderBook b;
        heap_runs.push_back(run_benchmark(b, workload));

        ArrayOrderBook c;
        array_runs.push_back(run_benchmark(c, workload));

        SegmentTreeOrderBook d;
        segment_tree_runs.push_back(run_benchmark(d, workload));

        std::cout << "Completed run " << run
                  << '/' << RUN_COUNT << '\n';
    }

    // Every implementation must produce identical checksums.
    const std::uint64_t expected_checksum =
        map_runs.front().checksum;

    for (int i = 0; i < RUN_COUNT; ++i) {
        require(
            map_runs[i].checksum == expected_checksum,
            "Map checksum changed between runs"
        );

        require(
            heap_runs[i].checksum == expected_checksum,
            "Heap checksum mismatch"
        );

        require(
            array_runs[i].checksum == expected_checksum,
            "Array checksum mismatch"
        );

        require(
            segment_tree_runs[i].checksum == expected_checksum,
            "Segment-tree checksum mismatch"
        );
    }

    std::cout << "\nAll checksums match: "
              << expected_checksum << '\n';

    print_results("A: std::map", map_runs);
    print_results("B: heap + lazy deletion", heap_runs);
    print_results("C: fixed-size array", array_runs);
    print_results("D: segment tree", segment_tree_runs);

    std::cout << "\nBenchmark completed successfully.\n";

    return 0;
}

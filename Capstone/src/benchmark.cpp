#include "order_books.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using Clock = std::chrono::steady_clock;

enum class OpType { Add, Cancel, Bbo };

struct Operation {
    OpType type;
    std::uint64_t id{};
    Side side{Side::Buy};
    int price_ticks{};
    long qty{};
};

struct RunResult {
    double total_ms{};
    double add_ns{};
    double cancel_ns{};
    double bbo_ns{};
    std::uint64_t checksum{};
    BBO final_bbo{};
};

std::vector<Operation> make_workload(std::size_t count) {
    std::mt19937 rng(42);
    std::uniform_int_distribution<int> operation(0, 99);
    std::uniform_int_distribution<int> quantity(1, 100);
    std::uniform_int_distribution<int> bid_price(5000, 9999);
    std::uniform_int_distribution<int> ask_price(10001, 15000);
    std::uniform_int_distribution<int> side_pick(0, 1);

    std::vector<Operation> result;
    result.reserve(count);
    std::vector<std::uint64_t> live_ids;
    live_ids.reserve(count);
    std::uint64_t next_id = 1;

    for (std::size_t i = 0; i < count; ++i) {
        const int choice = operation(rng);
        if (choice < 70 || live_ids.empty()) {
            const Side side = side_pick(rng) == 0 ? Side::Buy : Side::Sell;
            const int price = side == Side::Buy ? bid_price(rng) : ask_price(rng);
            result.push_back({OpType::Add, next_id, side, price, quantity(rng)});
            live_ids.push_back(next_id++);
        } else if (choice < 90) {
            std::uniform_int_distribution<std::size_t> live_pick(0, live_ids.size() - 1);
            const std::size_t index = live_pick(rng);
            const std::uint64_t id = live_ids[index];
            result.push_back({OpType::Cancel, id});
            live_ids[index] = live_ids.back();
            live_ids.pop_back();
        } else {
            result.push_back({OpType::Bbo});
        }
    }
    return result;
}

std::uint64_t mix(std::uint64_t checksum, std::uint64_t value) {
    return checksum ^ (value + 0x9e3779b97f4a7c15ULL + (checksum << 6) + (checksum >> 2));
}

std::uint64_t consume_bbo(std::uint64_t checksum, const BBO& bbo) {
    checksum = mix(checksum, bbo.has_bid);
    checksum = mix(checksum, bbo.has_ask);
    if (bbo.has_bid) {
        checksum = mix(checksum, static_cast<std::uint64_t>(bbo.bid_price * 100.0 + 0.5));
        checksum = mix(checksum, static_cast<std::uint64_t>(bbo.bid_qty));
    }
    if (bbo.has_ask) {
        checksum = mix(checksum, static_cast<std::uint64_t>(bbo.ask_price * 100.0 + 0.5));
        checksum = mix(checksum, static_cast<std::uint64_t>(bbo.ask_qty));
    }
    return checksum;
}

std::uint64_t consume_levels(std::uint64_t checksum, const std::vector<Level>& levels) {
    for (const Level& level : levels) {
        checksum = mix(checksum, static_cast<std::uint64_t>(level.price * 100.0 + 0.5));
        checksum = mix(checksum, static_cast<std::uint64_t>(level.qty));
    }
    return checksum;
}

template <class Book>
RunResult run_once(const std::vector<Operation>& workload) {
    Book book;
    std::uint64_t checksum = 0;
    std::uint64_t add_elapsed = 0, cancel_elapsed = 0, bbo_elapsed = 0;
    std::uint64_t add_count = 0, cancel_count = 0, bbo_count = 0;
    const auto total_start = Clock::now();

    for (std::size_t i = 0; i < workload.size(); ++i) {
        const Operation& op = workload[i];
        const auto start = Clock::now();
        if (op.type == OpType::Add) {
            book.add_order(op.id, op.side, ticks_to_price(op.price_ticks), op.qty);
            add_elapsed += static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start).count());
            ++add_count;
        } else if (op.type == OpType::Cancel) {
            checksum = mix(checksum, book.cancel_order(op.id));
            cancel_elapsed += static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start).count());
            ++cancel_count;
        } else {
            checksum = consume_bbo(checksum, book.get_bbo());
            bbo_elapsed += static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start).count());
            ++bbo_count;
        }

        if ((i + 1) % 100 == 0) {
            checksum = consume_levels(checksum, book.get_bids(10));
            checksum = consume_levels(checksum, book.get_asks(10));
        }
    }

    const double total_ms = std::chrono::duration<double, std::milli>(Clock::now() - total_start).count();
    return {total_ms,
            static_cast<double>(add_elapsed) / static_cast<double>(add_count),
            static_cast<double>(cancel_elapsed) / static_cast<double>(cancel_count),
            static_cast<double>(bbo_elapsed) / static_cast<double>(bbo_count),
            checksum,
            book.get_bbo()};
}

bool equal_bbo(const BBO& left, const BBO& right) {
    return left.has_bid == right.has_bid && left.has_ask == right.has_ask &&
           (!left.has_bid || (left.bid_price == right.bid_price && left.bid_qty == right.bid_qty)) &&
           (!left.has_ask || (left.ask_price == right.ask_price && left.ask_qty == right.ask_qty));
}

bool equal_levels(const std::vector<Level>& left, const std::vector<Level>& right) {
    if (left.size() != right.size()) return false;
    for (std::size_t i = 0; i < left.size(); ++i)
        if (left[i].price != right[i].price || left[i].qty != right[i].qty) return false;
    return true;
}

bool verify(const std::vector<Operation>& workload) {
    MapOrderBook a;
    IndexedOrderBook b;
    ArrayOrderBook c;
    std::cout << "Running correctness test with " << workload.size() << " operations...\n\n";
    for (std::size_t i = 0; i < workload.size(); ++i) {
        const Operation& op = workload[i];
        if (op.type == OpType::Add) {
            const double price = ticks_to_price(op.price_ticks);
            a.add_order(op.id, op.side, price, op.qty);
            b.add_order(op.id, op.side, price, op.qty);
            c.add_order(op.id, op.side, price, op.qty);
        } else if (op.type == OpType::Cancel) {
            const bool ar = a.cancel_order(op.id);
            const bool br = b.cancel_order(op.id);
            const bool cr = c.cancel_order(op.id);
            if (ar != br || ar != cr) {
                std::cerr << "Cancellation mismatch after operation " << i + 1 << '\n';
                return false;
            }
        } else {
            // The comparisons below also cover every explicit BBO query.
            (void)a.get_bbo();
            (void)b.get_bbo();
            (void)c.get_bbo();
        }

        const BBO abbo = a.get_bbo();
        const BBO bbbo = b.get_bbo();
        const BBO cbbo = c.get_bbo();
        const auto abids = a.get_bids(10), bbids = b.get_bids(10), cbids = c.get_bids(10);
        const auto aasks = a.get_asks(10), basks = b.get_asks(10), casks = c.get_asks(10);
        if (!equal_bbo(abbo, bbbo) || !equal_bbo(abbo, cbbo) ||
            !equal_levels(abids, bbids) || !equal_levels(abids, cbids) ||
            !equal_levels(aasks, basks) || !equal_levels(aasks, casks)) {
            std::cerr << "Book mismatch after operation " << i + 1 << '\n';
            return false;
        }
    }

    std::cout << "Implementation A: PASS\n"
              << "Implementation B: PASS\n"
              << "Implementation C: PASS\n\n"
              << "All implementations produce identical results.\n";
    return true;
}

double median(std::vector<double> values) {
    std::sort(values.begin(), values.end());
    return values[values.size() / 2];
}

template <class Book>
void benchmark(const std::string& name, const std::vector<Operation>& workload, int runs) {
    std::vector<RunResult> results;
    results.reserve(static_cast<std::size_t>(runs));
    for (int i = 0; i < runs; ++i) results.push_back(run_once<Book>(workload));

    std::vector<double> total, add, cancel, bbo;
    for (const RunResult& result : results) {
        total.push_back(result.total_ms);
        add.push_back(result.add_ns);
        cancel.push_back(result.cancel_ns);
        bbo.push_back(result.bbo_ns);
    }
    const RunResult& final = results.back();
    std::cout << "Implementation " << name << "\n"
              << "Operations: " << workload.size() << "\n"
              << "Runs: " << runs << " (median reported)\n"
              << std::fixed << std::setprecision(2)
              << "Total time (ms): " << median(total) << '\n'
              << "ns / add: " << median(add) << '\n'
              << "ns / cancel: " << median(cancel) << '\n'
              << "ns / BBO: " << median(bbo) << "\n\n"
              << "Final BBO:\n";
    if (final.final_bbo.has_bid)
        std::cout << "Bid: " << final.final_bbo.bid_price << " x " << final.final_bbo.bid_qty << '\n';
    else std::cout << "Bid: empty\n";
    if (final.final_bbo.has_ask)
        std::cout << "Ask: " << final.final_bbo.ask_price << " x " << final.final_bbo.ask_qty << '\n';
    else std::cout << "Ask: empty\n";
    std::cout << "Checksum: " << final.checksum << '\n';
}

void usage(const char* program) {
    std::cout << "Usage:\n  " << program << " --verify [--ops N]\n  "
              << program << " --impl A|B|C [--ops N] [--runs N]\n";
}
} // namespace

int main(int argc, char** argv) {
    try {
        bool do_verify = false;
        char implementation = '\0';
        std::size_t operation_count = 1'000'000;
        int runs = 5;
        for (int i = 1; i < argc; ++i) {
            const std::string argument = argv[i];
            if (argument == "--verify") do_verify = true;
            else if (argument == "--impl" && i + 1 < argc) implementation = argv[++i][0];
            else if (argument == "--ops" && i + 1 < argc) operation_count = std::stoull(argv[++i]);
            else if (argument == "--runs" && i + 1 < argc) runs = std::stoi(argv[++i]);
            else {
                usage(argv[0]);
                return 2;
            }
        }
        if (operation_count == 0 || runs <= 0) throw std::invalid_argument("ops and runs must be positive");
        const auto workload = make_workload(operation_count);
        if (do_verify) return verify(workload) ? 0 : 1;
        switch (implementation) {
            case 'A': case 'a': benchmark<MapOrderBook>("A", workload, runs); break;
            case 'B': case 'b': benchmark<IndexedOrderBook>("B", workload, runs); break;
            case 'C': case 'c': benchmark<ArrayOrderBook>("C", workload, runs); break;
            default: usage(argv[0]); return 2;
        }
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << '\n';
        return 1;
    }
}

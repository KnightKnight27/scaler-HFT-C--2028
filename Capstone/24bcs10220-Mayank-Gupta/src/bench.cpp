#include "array_book.hpp"
#include "map_book.hpp"
#include "vector_book.hpp"
#include "verify.hpp"
#include "workload.hpp"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <limits>
#include <string>

namespace {

using Clock = std::chrono::steady_clock;

struct Options {
    char implementation = 'A';
    bool verify = false;
    bool profile = false;
    std::size_t operations = 1000000;
    std::uint32_t seed = 42;
    unsigned runs = 5;
};

void usage() {
    std::cout << "Usage: ./bench [--impl A|B|C] [--ops N] [--seed N] [--runs N]\n"
                 "       ./bench --verify [--ops N] [--seed N]\n"
                 "       ./bench --impl A|B|C --profile [--ops N] [--seed N]\n\n"
                 "Default: 1,000,000 operations, seed 42, five measured runs.\n"
                 "Verify: edge cases and 20,000 random operations against a reference.\n"
                 "Profile: one replay without per-operation clock calls, for Linux perf.\n";
}

std::uint64_t number(const std::string& text, const std::string& option) {
    std::uint64_t value = 0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size()) {
        throw std::invalid_argument("invalid number for " + option);
    }
    return value;
}

Options parse(int argc, char** argv) {
    Options options;
    bool explicit_ops = false;
    bool explicit_runs = false;
    bool explicit_impl = false;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--verify") {
            options.verify = true;
        } else if (arg == "--profile") {
            options.profile = true;
        } else if (arg == "--impl" || arg == "--ops" || arg == "--seed" || arg == "--runs") {
            if (++i == argc) throw std::invalid_argument("missing value for " + arg);
            const std::string value = argv[i];
            if (arg == "--impl") {
                if (value != "A" && value != "B" && value != "C") {
                    throw std::invalid_argument("--impl must be A, B or C");
                }
                options.implementation = value[0];
                explicit_impl = true;
                continue;
            }
            const auto parsed = number(value, arg);
            if (arg == "--ops") {
                if (parsed == 0 || parsed > std::numeric_limits<std::size_t>::max()) {
                    throw std::invalid_argument("--ops must be a positive size");
                }
                options.operations = static_cast<std::size_t>(parsed);
                explicit_ops = true;
            } else if (arg == "--seed") {
                if (parsed > std::numeric_limits<std::uint32_t>::max()) {
                    throw std::invalid_argument("--seed must fit in uint32_t");
                }
                options.seed = static_cast<std::uint32_t>(parsed);
            } else {
                if (parsed == 0 || parsed > std::numeric_limits<unsigned>::max()) {
                    throw std::invalid_argument("--runs must be a positive unsigned integer");
                }
                options.runs = static_cast<unsigned>(parsed);
                explicit_runs = true;
            }
        } else {
            throw std::invalid_argument("unknown option: " + arg);
        }
    }
    if (options.verify && (options.profile || explicit_runs || explicit_impl)) {
        throw std::invalid_argument("--verify checks all implementations; use only --ops and --seed with it");
    }
    if (options.profile && explicit_runs) {
        throw std::invalid_argument("--profile always uses one replay; omit --runs");
    }
    if (options.verify && !explicit_ops) options.operations = 20000;
    if (options.profile) options.runs = 1;
    return options;
}

double nanoseconds(Clock::duration duration) {
    return std::chrono::duration<double, std::nano>(duration).count();
}

// Unsigned overflow is intentional. Every query result contributes to the checksum.
void mix(std::uint64_t& checksum, std::uint64_t value) {
    checksum = (checksum ^ value) * 1099511628211ULL;
}

void consume(std::uint64_t& checksum, const ob::BBO& bbo) {
    mix(checksum, bbo.has_bid);
    mix(checksum, bbo.bid_price);
    mix(checksum, static_cast<std::uint64_t>(bbo.bid_qty));
    mix(checksum, bbo.has_ask);
    mix(checksum, bbo.ask_price);
    mix(checksum, static_cast<std::uint64_t>(bbo.ask_qty));
}

void consume(std::uint64_t& checksum, const std::vector<ob::Level>& levels) {
    mix(checksum, levels.size());
    for (const auto& level : levels) {
        mix(checksum, level.price);
        mix(checksum, static_cast<std::uint64_t>(level.qty));
    }
}

struct Result {
    double total_ms = 0;
    double add_ns = 0;
    double cancel_ns = 0;
    double bbo_ns = 0;
    std::uint64_t checksum = 14695981039346656037ULL;
    ob::BBO final_bbo;
};

template <typename Book, bool MeasureOperations>
Result replay(const std::vector<ob::Operation>& workload) {
    Book book;
    Result result;
    std::size_t adds = 0;
    std::size_t cancels = 0;
    std::size_t queries = 0;
    const auto total_start = Clock::now();
    for (std::size_t i = 0; i < workload.size(); ++i) {
        const auto& op = workload[i];
        switch (op.type) {
        case ob::OpType::Add: {
            Clock::time_point start;
            if constexpr (MeasureOperations) start = Clock::now();
            book.add_order(op.id, op.side, op.price, op.qty);
            if constexpr (MeasureOperations) result.add_ns += nanoseconds(Clock::now() - start);
            ++adds;
            break;
        }
        case ob::OpType::Cancel: {
            Clock::time_point start;
            if constexpr (MeasureOperations) start = Clock::now();
            const bool cancelled = book.cancel_order(op.id);
            if constexpr (MeasureOperations) result.cancel_ns += nanoseconds(Clock::now() - start);
            if (!cancelled) throw std::runtime_error("benchmark cancellation failed");
            mix(result.checksum, cancelled);
            ++cancels;
            break;
        }
        case ob::OpType::Bbo: {
            Clock::time_point start;
            if constexpr (MeasureOperations) start = Clock::now();
            const auto bbo = book.get_bbo();
            if constexpr (MeasureOperations) result.bbo_ns += nanoseconds(Clock::now() - start);
            consume(result.checksum, bbo);
            ++queries;
            break;
        }
        }
        if ((i + 1) % 100 == 0) {
            consume(result.checksum, book.get_bids(10));
            consume(result.checksum, book.get_asks(10));
        }
    }
    result.total_ms = nanoseconds(Clock::now() - total_start) / 1000000.0;
    result.final_bbo = book.get_bbo();
    consume(result.checksum, result.final_bbo);
    if (adds) result.add_ns /= static_cast<double>(adds);
    if (cancels) result.cancel_ns /= static_cast<double>(cancels);
    if (queries) result.bbo_ns /= static_cast<double>(queries);
    return result;
}

double median(std::vector<double> values) {
    std::sort(values.begin(), values.end());
    const auto middle = values.size() / 2;
    return values.size() % 2 ? values[middle] : (values[middle - 1] + values[middle]) / 2;
}

void print_average(double value, std::size_t count) {
    if (count) std::cout << value;
    else std::cout << "n/a";
}

void print_bbo(const ob::BBO& bbo) {
    std::cout << "Final BBO: bid ";
    if (bbo.has_bid) std::cout << bbo.bid_price / 100.0 << " x " << bbo.bid_qty;
    else std::cout << "none";
    std::cout << ", ask ";
    if (bbo.has_ask) std::cout << bbo.ask_price / 100.0 << " x " << bbo.ask_qty;
    else std::cout << "none";
    std::cout << '\n';
}

template <typename Book>
void benchmark(const Options& options) {
    // Build once, outside the measured interval; every fresh book replays these same operations.
    const auto workload = ob::make_workload(options.operations, options.seed);
    std::size_t adds = 0;
    std::size_t cancels = 0;
    for (const auto& op : workload) {
        adds += op.type == ob::OpType::Add;
        cancels += op.type == ob::OpType::Cancel;
    }
    std::cout << "Implementation: " << options.implementation << '\n'
              << "Mode: " << (options.profile ? "profile (no per-operation timers)" : "timed") << '\n'
              << "Operations: " << workload.size() << ", seed: " << options.seed
              << ", runs: " << options.runs << '\n'
              << "Adds: " << adds << ", cancels: " << cancels
              << ", BBO queries: " << workload.size() - adds - cancels << '\n'
              << "Depth queries per side: " << workload.size() / 100
              << ", final live orders: " << adds - cancels << '\n';
    if (!options.profile) std::cout << "run,total_ms,ns/add,ns/cancel,ns/BBO\n";

    std::vector<double> totals, add_times, cancel_times, bbo_times;
    std::uint64_t checksum = 0;
    ob::BBO final_bbo;
    for (unsigned run = 0; run < options.runs; ++run) {
        const auto result = options.profile ? replay<Book, false>(workload) : replay<Book, true>(workload);
        if (run != 0 && (result.checksum != checksum || !(result.final_bbo == final_bbo))) {
            throw std::runtime_error("repeated runs produced different results");
        }
        checksum = result.checksum;
        final_bbo = result.final_bbo;
        totals.push_back(result.total_ms);
        add_times.push_back(result.add_ns);
        cancel_times.push_back(result.cancel_ns);
        bbo_times.push_back(result.bbo_ns);
        if (!options.profile) {
            std::cout << run + 1 << ',' << result.total_ms << ',';
            print_average(result.add_ns, adds);
            std::cout << ',';
            print_average(result.cancel_ns, cancels);
            std::cout << ',';
            print_average(result.bbo_ns, workload.size() - adds - cancels);
            std::cout << '\n';
        }
    }
    if (options.profile) {
        std::cout << "Replay total (ms): " << totals.front() << '\n';
    } else {
        std::cout << "median," << median(totals) << ',';
        print_average(median(add_times), adds);
        std::cout << ',';
        print_average(median(cancel_times), cancels);
        std::cout << ',';
        print_average(median(bbo_times), workload.size() - adds - cancels);
        std::cout << '\n';
    }
    print_bbo(final_bbo);
    std::cout << "Checksum: " << checksum << '\n';
}

} // namespace

int main(int argc, char** argv) {
    try {
        if (argc == 2 && std::string(argv[1]) == "--help") {
            usage();
            return 0;
        }
        const auto options = parse(argc, argv);
        std::cout << std::fixed << std::setprecision(2);
        if (options.verify) {
            ob::verify(options.operations, options.seed);
        } else if (options.implementation == 'A') {
            benchmark<ob::MapBook>(options);
        } else if (options.implementation == 'B') {
            benchmark<ob::VectorBook>(options);
        } else {
            benchmark<ob::ArrayBook>(options);
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << '\n';
        return 1;
    }
}

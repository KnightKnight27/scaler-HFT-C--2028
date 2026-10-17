#include "array_book.hpp"
#include "map_book.hpp"
#include "vector_book.hpp"
#include "workload.hpp"
#include <chrono>
#include <iomanip>
#include <iostream>
#include <limits>
#include <string>

using namespace ob;
using Clock = std::chrono::steady_clock;

struct Result {
    double total_ms = 0;
    std::array<double, 3> ns{};
    std::uint64_t checksum = 0;
    BBO bbo;
};

template<class Book>
Result replay(const Workload& workload, bool time_operations) {
    Book book(workload.peak_live);
    Result result;
    std::array<Clock::duration, 3> elapsed{};
    const auto begin = Clock::now();
    for (std::size_t i = 0; i < workload.operations.size(); ++i) {
        const auto& op = workload.operations[i];
        if (time_operations) {
            const auto start = Clock::now();
            BBO bbo;
            bool cancelled = false;
            switch (op.kind) {
            case Kind::Add: book.add_order(op.id, op.side, op.price, op.qty); break;
            case Kind::Cancel: cancelled = book.cancel_order(op.id); break;
            case Kind::Query: bbo = book.get_bbo(); break;
            }
            const auto stop = Clock::now();
            elapsed[static_cast<int>(op.kind)] += stop - start;
            if (op.kind == Kind::Query) consume(result.checksum, bbo);
            if (op.kind == Kind::Cancel) {
                if (!cancelled) throw std::runtime_error("benchmark cancelled a non-live ID");
                mix(result.checksum, cancelled);
            }
        } else if (!apply(book, op, result.checksum)) {
            throw std::runtime_error("benchmark cancelled a non-live ID");
        }
        if ((i + 1) % 100 == 0) {
            consume(result.checksum, book.get_bids(10));
            consume(result.checksum, book.get_asks(10));
        }
    }
    result.total_ms = std::chrono::duration<double, std::milli>(Clock::now() - begin).count();
    result.bbo = book.get_bbo();
    for (int i = 0; i < 3; ++i)
        if (workload.counts[i] != 0)
            result.ns[i] = std::chrono::duration<double, std::nano>(elapsed[i]).count() / workload.counts[i];
    return result;
}

static double median(std::vector<double> values) {
    std::sort(values.begin(), values.end());
    const auto mid = values.size() / 2;
    return values.size() % 2 ? values[mid] : (values[mid - 1] + values[mid]) / 2;
}

static void print_bbo(const BBO& bbo) {
    auto price = [](Price p) {
        std::cout << p / 100 << '.' << std::setfill('0') << std::setw(2) << p % 100 << std::setfill(' ');
    };
    std::cout << "Final BBO: Bid: ";
    if (bbo.has_bid) { price(bbo.bid_price); std::cout << " x " << bbo.bid_qty; }
    else std::cout << "empty";
    std::cout << " Ask: ";
    if (bbo.has_ask) { price(bbo.ask_price); std::cout << " x " << bbo.ask_qty; }
    else std::cout << "empty";
    std::cout << '\n';
}

template<class Book>
void benchmark(const Workload& workload, int runs, bool total_only) {
    std::vector<double> totals;
    std::array<std::vector<double>, 3> latencies;
    Result last;
    for (int i = 0; i < runs; ++i) {
        auto total = replay<Book>(workload, false);
        if (i > 0 && (total.checksum != last.checksum || !(total.bbo == last.bbo)))
            throw std::runtime_error("repeated trial disagrees");
        totals.push_back(total.total_ms);
        std::cout << "Trial " << i + 1 << ": total_ms=" << total.total_ms;
        if (!total_only) {
            auto separate = replay<Book>(workload, true);
            if (total.checksum != separate.checksum || !(total.bbo == separate.bbo))
                throw std::runtime_error("timed operation replay disagrees with total replay");
            for (int j = 0; j < 3; ++j) latencies[j].push_back(separate.ns[j]);
            std::cout << " ns/add=" << separate.ns[0] << " ns/cancel=" << separate.ns[1]
                      << " ns/BBO=" << separate.ns[2];
        }
        std::cout << '\n';
        last = total;
    }
    std::cout << "Median total time (ms): " << median(totals) << '\n';
    if (!total_only) {
        std::cout << "Median ns/add: " << median(latencies[0]) << '\n'
                  << "Median ns/cancel: " << median(latencies[1]) << '\n'
                  << "Median ns/BBO: " << median(latencies[2]) << '\n';
        std::vector<double> clock_cost;
        double clock_sum = 0;
        double smallest_positive = std::numeric_limits<double>::infinity();
        for (int i = 0; i < 10000; ++i) {
            auto start = Clock::now();
            auto end = Clock::now();
            const auto ns = std::chrono::duration<double, std::nano>(end - start).count();
            clock_cost.push_back(ns);
            clock_sum += ns;
            if (ns > 0) smallest_positive = std::min(smallest_positive, ns);
        }
        std::cout << "Median empty clock interval (ns, not subtracted): " << median(clock_cost) << '\n';
        std::cout << "Mean empty clock interval (ns, not subtracted): " << clock_sum / clock_cost.size() << '\n'
                  << "Smallest positive empty interval (ns): " << smallest_positive << '\n';
    }
    print_bbo(last.bbo);
    std::cout << "Checksum: " << last.checksum << '\n';
}

static std::uint64_t number(const std::string& value) {
    if (value.empty() || value.find_first_not_of("0123456789") != std::string::npos)
        throw std::invalid_argument("expected a nonnegative integer");
    return std::stoull(value);
}

int main(int argc, char** argv) {
    try {
        char impl = 'A';
        bool checking = false, total_only = false;
        std::size_t count = 1000000;
        std::uint32_t seed = 42;
        int runs = 5;
        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "--verify") checking = true;
            else if (arg == "--total-only") total_only = true;
            else if (arg == "--help") {
                std::cout << "bench [--impl A|B|C] [--verify] [--ops N] [--seed N] [--runs N] [--total-only]\n"
                          << "Defaults: 1000000 operations, seed 42, 5 trials.\n";
                return 0;
            } else {
                if (i + 1 == argc) throw std::invalid_argument("missing option value");
                std::string value = argv[++i];
                if (arg == "--impl") {
                    if (value != "A" && value != "B" && value != "C")
                        throw std::invalid_argument("implementation must be A, B or C");
                    impl = value[0];
                } else if (arg == "--ops") {
                    auto n = number(value);
                    if (n == 0 || n > std::numeric_limits<std::uint32_t>::max())
                        throw std::invalid_argument("ops must be in 1..UINT32_MAX");
                    count = static_cast<std::size_t>(n);
                } else if (arg == "--seed") {
                    auto n = number(value);
                    if (n > std::numeric_limits<std::uint32_t>::max()) throw std::invalid_argument("seed too large");
                    seed = static_cast<std::uint32_t>(n);
                } else if (arg == "--runs") {
                    auto n = number(value);
                    if (n == 0 || n > 1000) throw std::invalid_argument("runs must be in 1..1000");
                    runs = static_cast<int>(n);
                } else throw std::invalid_argument("unknown option: " + arg);
            }
        }
        std::cout << std::fixed << std::setprecision(3);
        const auto workload = make_workload(count, seed);
        std::cout << "Operations: " << count << " Seed: " << seed << "\n"
                  << "Add: " << workload.counts[0] << " Cancel: " << workload.counts[1]
                  << " BBO: " << workload.counts[2] << " Depth pairs: " << count / 100 << '\n'
                  << "Peak live orders: " << workload.peak_live << '\n'
                  << "Workload checksum: " << workload.checksum << '\n' << std::flush;
        if (checking) { run_edge_tests(); verify(workload); }
        else {
            std::cout << "Implementation: " << impl << " Runs: " << runs << '\n';
            switch (impl) {
            case 'A': benchmark<MapBook>(workload, runs, total_only); break;
            case 'B': benchmark<VectorBook>(workload, runs, total_only); break;
            case 'C': benchmark<ArrayBook>(workload, runs, total_only); break;
            }
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
